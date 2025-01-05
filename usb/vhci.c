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
#include "usbip.h"
#include "usbthing.h"

struct Priv {
	int sockfd;
	uint8_t *buffer;
	size_t buffer_length;
};

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

static int handle_submit(struct UsbThing *ctx, int sockfd, struct usbip_header *header) {
	int resp_len = 0;
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
	resp.u.ret_submit.status = bswap_32(0);
	resp.u.ret_submit.start_frame = bswap_32(0);
	resp.u.ret_submit.number_of_packets = bswap_32(0);
	resp.u.ret_submit.error_count = bswap_32(0);

	uint8_t *buffer = NULL;

	if (ep == 0) {
		buffer = malloc(65535);
		// Handle control request payloads
		int payload_size = 0;
		if (dir == 0 && len != 0) {
			int rc = recv(sockfd, &header->u.cmd_submit.setup[8], len, 0);
			if (rc != len) return -1;
			payload_size += (int)len;
		}

		int rc = ctx->handle_control_request(ctx, 0, (int)ep, header->u.cmd_submit.setup, 8 + payload_size, buffer);
		if (rc < 0) return rc;
		resp_len = rc;
		if (dir == 0 && len != 0 && resp_len != 0) {
			printf("Illegal double data phase in control request\n");
			abort();
		}
		resp.u.ret_submit.actual_length = bswap_32(resp_len);
	} else if (dir == 1) {
		buffer = malloc(len);
		assert(buffer != NULL);

		resp_len = ctx->handle_bulk_transfer(ctx, 0, (int)ep_addr, buffer, (int)len);
		if (resp_len == -1) {
			return 0;
		}
		resp.u.ret_submit.actual_length = bswap_32(resp_len);
	} else if (dir == 0) {
		buffer = malloc(len);
		assert(buffer != NULL);

		int read = 0;
		for (int i = 0; i < 10; i++) {
			printf("Trying again...\n");
			int rc = recv(sockfd, buffer + read, len - read, 0);
			assert(rc >= 0);
			read += rc;
			if (read == len) break;
		}
		assert(read == len);

		ctx->handle_bulk_transfer(ctx, 0, (int)ep_addr, buffer, (int)len);
		resp_len = 0;
		resp.u.ret_submit.actual_length = bswap_32(len);
	} else {
		printf("Illegal state\n");
		abort();
	}

	send(sockfd, &resp, 0x30, 0);
	if (resp_len && buffer != NULL)
		send(sockfd, buffer, resp_len, 0);

	if (buffer != NULL)
		free(buffer);

	return 0;
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
	int speed = 2;

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
		char packet[512] = {0};
		int rc = recv(sockfd, packet, sizeof(struct usbip_header), 0);
		if (rc < 0) {
			printf("Failed to receive data %d\n", errno);
			return -1;
		} else if (rc == 0) {
			continue;
		} else if (rc != sizeof(struct usbip_header)) {
			printf("Received partial packet %d\n", rc);
			abort();
		}
		//printf("Received %d\n", rc);

		struct usbip_header *header = (struct usbip_header *)packet;
		uint32_t command = bswap_32(header->base.command);
		switch (command) {
		case USBIP_CMD_SUBMIT:
			rc = handle_submit(ctx, sockfd, header);
			if (rc) goto exit;
			break;
		case USBIP_CMD_UNLINK: {
			printf("USBIP_CMD_UNLINK\n");
			struct usbip_header resp = {0};
			resp.base.command = bswap_32(USBIP_RET_UNLINK);
			resp.base.seqnum = header->base.seqnum;
			resp.base.devid = header->base.devid;
			resp.base.direction = header->base.direction;
			resp.base.ep = header->base.ep;
			resp.u.ret_unlink.status = bswap_32(0);
			send(sockfd, &resp, 48, 0);
			} break;
		case USBIP_RESET_DEV:
			printf("USBIP_RESET_DEV\n");
			break;
		default:
			printf("Unknown usbip command %x\n", command);
			hexdump(header, rc);
			abort();
		}
	}

	close(fd);
	return 0;

	exit:;
	close(fd);
	return -1;
}
