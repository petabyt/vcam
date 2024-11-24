#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <stdint.h>
#include "usbip.h"

int main() {
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

	int sockets[2] = {-1,-1};
	int ir = socketpair(AF_UNIX, SOCK_STREAM, 0, sockets);

	//int sockfd = socket(AF_UNIX, SOCK_STREAM, 0);

	int port = 0;
	int devid = 1;
	int speed = 2;

	char cmd[255];
	sprintf(cmd, "%d %d %d %d", port, sockets[1], devid, speed);
	if (write(fd, cmd, strlen(cmd)) != strlen(cmd)) {
		printf("Failed to write attach cmd %d\n", errno);
		return -1;
	}

	close(sockets[1]);

	int sockfd = sockets[0];

	while (1) {
		char buffer[512];
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
			printf("Submit\n");
			break;
		case USBIP_CMD_UNLINK:
			printf("Unlink\n");
			break;
		case USBIP_RESET_DEV:
			printf("Reset device\n");
		default:
			printf("... %u\n", __bswap_32(header->base.command));
			break;
		}
		
		printf("Got data %d\n", rc);
	}

	close(fd);

	return 0;
}
