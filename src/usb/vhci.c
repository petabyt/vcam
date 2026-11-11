#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <stdint.h>
#include <stdlib.h>
#include <byteswap.h>
#include <assert.h>
#include <vcam.h>
#include "usbip.h"
#include "usbthing.h"

struct Priv {
	int sockfd;
	uint8_t *buffer;
	size_t buffer_length;
};

// Result codes of the usbip request handlers: 0 = served (response sent or
// URB deliberately parked), PEER_CLOSED = the kernel side of the socketpair
// is gone (client exit / detach) and the serving loop must end.
#define PEER_CLOSED 1

static void hexdump(void *buffer, int size) {
	unsigned char *buf = (unsigned char *)buffer;
	for (int i = 0; i < size; i++) {
		printf("%02x ", buf[i]);
		if ((i + 1) % 16 == 0) {
			printf("\n");
		}
	}
	printf("\n");
}

// send() that cannot raise SIGPIPE and loops over partial writes.
// Returns 0, or -1 when the peer is gone / the socket failed.
static int send_all(int sockfd, const void *buf, size_t len) {
	const char *p = buf;
	size_t left = len;

	while (left > 0) {
		ssize_t rc = send(sockfd, p, left, MSG_NOSIGNAL);
		if (rc < 0) {
			if (errno == EINTR) continue;
			return -1;
		}
		p += rc;
		left -= rc;
	}
	return 0;
}

// recv() exactly len bytes. Returns 0, PEER_CLOSED when the kernel closed
// the session, or -1 on other errors (EINTR is retried).
static int recv_full(int sockfd, void *buf, size_t len) {
	char *p = buf;
	size_t left = len;

	while (left > 0) {
		ssize_t rc = recv(sockfd, p, left, 0);
		if (rc == 0) return PEER_CLOSED;
		if (rc < 0) {
			if (errno == EINTR) continue;
			return -1;
		}
		p += rc;
		left -= rc;
	}
	return 0;
}

// Complete a URB with an error status instead of leaving it pending forever.
static int respond_ret_submit_status(int sockfd, struct usbip_header *header, int32_t status) {
	struct usbip_header resp = {0};
	resp.base.command = bswap_32(USBIP_RET_SUBMIT);
	resp.base.seqnum = header->base.seqnum;
	resp.base.devid = header->base.devid;
	resp.base.direction = header->base.direction;
	resp.base.ep = header->base.ep;
	resp.u.ret_submit.status = bswap_32(status);

	if (send_all(sockfd, &resp, sizeof(resp))) return PEER_CLOSED;
	return 0;
}

// Serves one CMD_SUBMIT. Returns 0 when the request was handled (response
// sent, or the URB deliberately parked) and the session continues;
// nonzero ends the session.
static int handle_submit(struct UsbThing *ctx, int sockfd, struct usbip_header *header) {
	uint32_t len = bswap_32(header->u.cmd_submit.transfer_buffer_length);
	uint32_t dir = bswap_32(header->base.direction);
	uint32_t ep = bswap_32(header->base.ep);
	uint32_t ep_addr = (dir << 7) | ep;
	usbt_dbg("submit ep:%x len:%d dir:%d\n", ep_addr, len, dir);

	struct usbip_header resp = {0};
	resp.base.command = bswap_32(USBIP_RET_SUBMIT);
	resp.base.seqnum = header->base.seqnum;
	resp.base.devid = header->base.devid;
	resp.base.direction = header->base.direction;
	resp.base.ep = header->base.ep;

	uint8_t *buffer = NULL;
	int resp_len = 0;
	int rc;

	if (ep == 0) {
		// Control request; OUT requests carry their payload after the header
		buffer = malloc(65535);
		if (buffer == NULL) return respond_ret_submit_status(sockfd, header, -ENOMEM);
		if (dir == 0 && len != 0) {
			rc = recv_full(sockfd, &header->u.cmd_submit.setup[8], len);
			if (rc) goto peer_or_error;
		}
		resp_len = ctx->handle_control_request(ctx, 0, (int)ep, header->u.cmd_submit.setup, 8 + (dir == 0 ? (int)len : 0), buffer);
		if (resp_len < 0) {
			free(buffer);
			return respond_ret_submit_status(sockfd, header, -EIO);
		}
		if (dir == 0 && len != 0 && resp_len != 0) {
			printf("Illegal double data phase in control request\n");
			abort();
		}
	} else if (dir == 1) {
		// Bulk/interrupt IN. Interrupt IN with no event due parks the URB:
		// no RET_SUBMIT is sent and the kernel keeps it pending until the
		// client times out and unlinks it.
		buffer = malloc(len ? len : 1);
		if (buffer == NULL) return respond_ret_submit_status(sockfd, header, -ENOMEM);
		resp_len = ctx->handle_bulk_transfer(ctx, 0, (int)ep_addr, buffer, (int)len);
		if (resp_len == GP_ERROR_TIMEOUT) {
			free(buffer);
			return 0;
		}
		if (resp_len < 0) {
			free(buffer);
			return respond_ret_submit_status(sockfd, header, -EIO);
		}
	} else if (dir == 0) {
		// Bulk/interrupt OUT, payload follows the header
		buffer = malloc(len ? len : 1);
		if (buffer == NULL) return respond_ret_submit_status(sockfd, header, -ENOMEM);
		rc = recv_full(sockfd, buffer, len);
		if (rc) goto peer_or_error;
		ctx->handle_bulk_transfer(ctx, 0, (int)ep_addr, buffer, (int)len);
		resp_len = 0;
	} else {
		printf("Illegal state\n");
		abort();
	}

	// For host->device bulk/interrupt transfers the URB completes with
	// actual_length = transfer length; for control and IN transfers it is
	// the returned data length
	if (ep != 0 && dir == 0)
		resp.u.ret_submit.actual_length = bswap_32((int)len);
	else
		resp.u.ret_submit.actual_length = bswap_32(resp_len);
	if (send_all(sockfd, &resp, sizeof(resp))) goto peer_or_error;
	if (dir == 1 && resp_len > 0) {
		if (send_all(sockfd, buffer, resp_len)) goto peer_or_error;
	}
	free(buffer);
	return 0;

peer_or_error:
	free(buffer);
	return PEER_CLOSED;
}

int usbt_vhci_init(struct UsbThing *ctx) {
	const char *attach_path = "/sys/devices/platform/vhci_hcd.0/attach";

	int fd = open(attach_path, O_WRONLY);
	if (fd == -1) {
		if (errno == 2) {
			printf(
				"Kernel module not loaded, run:\n"
				"sudo modprobe vhci-hcd\n"
			);
			return -1;
		} if (errno == 13) {
			printf("Permission denied\n");
			return -1;
		}
		printf("Failed to open attach point %d\n", errno);
		return -1;
	}

	int sockets[2] = {-1, -1};
	int ir = socketpair(AF_UNIX, SOCK_STREAM, 0, sockets);
	if (ir == -1) return -1;

	int port = 0;
	int devid = 1;
	// USB_SPEED_HIGH: the descriptors advertise 512-byte bulk packets,
	// which usbcore would otherwise clamp to 64 on a full-speed attach
	int speed = 3;

	// Write the command to take over the port
	char cmd[255];
	sprintf(cmd, "%d %d %d %d", port, sockets[1], devid, speed);
	if (write(fd, cmd, strlen(cmd)) != strlen(cmd)) {
		printf("Failed to write attach cmd %d\n", errno);
		return -1;
	}

	close(sockets[1]);

	int sockfd = sockets[0];

	while (1) {
		struct usbip_header header;
		int rc = recv_full(sockfd, &header, sizeof(header));
		if (rc) {
			// The kernel detaches the device when our socket closes,
			// there is nothing to clean up on this side
			printf("usbip session ended (%s)\n",
			       rc == PEER_CLOSED ? "peer closed" : strerror(errno));
			break;
		}

		uint32_t command = bswap_32(header.base.command);
		switch (command) {
		case USBIP_CMD_SUBMIT:
			if (handle_submit(ctx, sockfd, &header)) {
				printf("usbip peer closed mid-transfer\n");
				close(fd);
				return 0;
			}
			break;
		case USBIP_CMD_UNLINK: {
			usbt_dbg("USBIP_CMD_UNLINK\n");
			struct usbip_header resp = {0};
			resp.base.command = bswap_32(USBIP_RET_UNLINK);
			resp.base.seqnum = header.base.seqnum;
			resp.base.devid = header.base.devid;
			resp.base.direction = header.base.direction;
			resp.base.ep = header.base.ep;
			resp.u.ret_unlink.status = 0;
			if (send_all(sockfd, &resp, sizeof(resp))) {
				printf("usbip peer closed on unlink response\n");
				close(fd);
				return 0;
			}
			} break;
		case USBIP_RESET_DEV:
			printf("USBIP_RESET_DEV\n");
			break;
		default:
			printf("Unknown usbip command %x\n", command);
			hexdump(&header, sizeof(header));
			abort();
		}
	}

	close(fd);
	return 0;
}
