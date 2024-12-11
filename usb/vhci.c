#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <stdint.h>
#include <stdlib.h>
#include <byteswap.h>
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

static int handle_submit(struct UsbThing *ctx, struct Priv *p, struct usbip_header *header) {
#if 0
	printf("%d --> ", bswap_32(header->u.cmd_submit.transfer_buffer_length));
	hexdump(header->u.cmd_submit.setup, 8);
#endif

	int resp_len = 0;
	if (header->base.ep == 0) {
		int rc = ctx->handle_control_request(ctx, 0, 0, header->u.cmd_submit.setup, 8, p->buffer);
		if (rc < 0) return rc;
		resp_len = rc;
	} else {
		printf("TODO: ep %d not implemented yet\n", header->base.ep);
		abort();
	}

	struct usbip_header resp = {0};
	resp.base.command = bswap_32(USBIP_RET_SUBMIT);
	resp.base.seqnum = header->base.seqnum;
	resp.base.devid = header->base.devid;
	resp.base.direction = header->base.direction;
	resp.base.ep = header->base.ep;

	resp.u.ret_submit.status = bswap_32(0);
	resp.u.ret_submit.actual_length = bswap_32(resp_len);
	resp.u.ret_submit.start_frame = bswap_32(0);
	resp.u.ret_submit.number_of_packets = bswap_32(0);
	resp.u.ret_submit.error_count = bswap_32(0);

	send(p->sockfd, &resp, 0x30, 0);
	send(p->sockfd, p->buffer, resp_len, 0);
#if 0
	printf("<-- ");
	hexdump(&resp, 0x30);
	printf("Sending %d bytes\n", resp_len);
	printf("<-- ");
	hexdump(p->buffer, resp_len);
	printf("Completed control request\n");
#endif
	return 0;
}

int usb_vhci_init(struct UsbThing *ctx) {
	struct Priv p = {0};
	// TODO: Set ctx->priv_backend to NULL once this function returns
	ctx->priv_backend = (void *)&p;

	p.buffer = malloc(1000);
	p.buffer_length = 1000;

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

	//int sockfd = socket(AF_UNIX, SOCK_STREAM, 0);

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
	p.sockfd = sockfd;

	while (1) {
		char buffer[512] = {0};
		int rc = recv(sockfd, buffer, sizeof(struct usbip_header), 0);
		if (rc < 0) {
			printf("Failed to receive data %d\n", errno);
			return -1;
		} else if (rc == 0) {
			continue;
		}

		struct usbip_header *header = (struct usbip_header *)buffer;
		switch (bswap_32(header->base.command)) {
		case USBIP_CMD_SUBMIT:
			rc = handle_submit(ctx, &p, header);
			if (rc) goto exit;
			break;
		case USBIP_CMD_UNLINK:
			printf("USBIP_CMD_UNLINK\n");
			break;
		case USBIP_RESET_DEV:
			printf("USBIP_RESET_DEV\n");
			break;
		default:
			printf("Unknown usbip command %x\n", bswap_32(header->base.command));
			break;
		}
	}

	close(fd);
	return 0;

	exit:;
	close(fd);
	return -1;
}
