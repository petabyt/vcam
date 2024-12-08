#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <stdint.h>
#include <stdlib.h>
#include "usbip.h"
#include "lib.h"

struct Priv {
	int sockfd;
	uint8_t control_buffer[512];
	int control_buffer_len;
};

int usb_data_to_host(struct UsbLib *ctx, int devn, int endpoint, void *data, int length) {
	struct Priv *p = ctx->priv_backend;
	memcpy(p->control_buffer, data, length);
	p->control_buffer_len += length;
	return 0;
}

int usb_vhci_init(void) {
	struct Priv p = {0};

	struct UsbLib ctx = {0};
	ctx.priv_backend = (void *)&p;

	const char *attach_path = "/sys/devices/platform/vhci_hcd.0/attach";

	int fd = open(attach_path, O_WRONLY);
	if (fd == -1) {
		if (errno == 2) {
			printf("Run sudo modprobe vhci-hcd\n");
			return -1;
		} if (errno == 13) {
			printf("Run as sudo\n");
			return -1;
		}
		printf("Failed to open attach point %d\n", errno);
		return -1;
	}

	int sockets[2] = {-1, -1};
	int ir = socketpair(AF_UNIX, SOCK_STREAM, 0, sockets);

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
		int rc = recv(sockfd, buffer, sizeof(struct usbip_header_basic), 0);
		if (rc < 0) {
			printf("Failed to receive data %d\n", errno);
			return -1;
		} else if (rc == 0) {
			continue;
		}

		struct usbip_header *header = (struct usbip_header *)buffer;
		switch (__bswap_32(header->base.command)) {
		case USBIP_CMD_SUBMIT:
			rc = recv(sockfd, &header->u.cmd_submit, sizeof(struct usbip_header_cmd_submit), 0);
			if (rc != sizeof(struct usbip_header_cmd_submit)) {
				printf("Didn't receive submit packet\n");
				goto exit;
			}
			p.control_buffer_len = 0;
			rc = usb_handle_control_request(&ctx, 0, 0, header->u.cmd_submit.setup, 8);

			struct usbip_header resp;
			resp.base.command = USBIP_RET_SUBMIT;
			resp.base.seqnum = header->base.seqnum;
			resp.base.devid = header->base.devid;
			resp.base.direction = header->base.direction;
			resp.base.ep = header->base.ep;

			resp.u.ret_submit.status = 0;
			resp.u.ret_submit.actual_length = p.control_buffer_len;
			resp.u.ret_submit.start_frame = 0;
			resp.u.ret_submit.number_of_packets = 0;
			resp.u.ret_submit.error_count = 0;

			send(sockfd, &resp, 40, 0);
			send(sockfd, p.control_buffer, p.control_buffer_len, 0);
			
			if (rc) goto exit;
			break;
		case USBIP_CMD_UNLINK:
			printf("Unlink\n");
			break;
		case USBIP_RESET_DEV:
			printf("Reset device\n");
		default:
			printf("Unknown usbip command %x, seqnum %u\n", __bswap_32(header->base.command), __bswap_32(header->base.seqnum));
			break;
		}
		
		printf("Got data %d\n", rc);
	}

	close(fd);

	exit:;
	close(fd);
	return -1;
}
