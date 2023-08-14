#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>

#include <ptp.h>

#define _GPHOTO2_INTERNAL_CODE
#define _DARWIN_C_SOURCE
#include <config.h>
#include <gphoto2/gphoto2-port-library.h>
#include <vcamera.h>
#include <gphoto2/gphoto2-port.h>
#include <gphoto2/gphoto2-port-result.h>
#include <gphoto2/gphoto2-port-log.h>
#include <libgphoto2_port/i18n.h>

void tester_log() {
	
}

struct _GPPortPrivateLibrary {
	int	isopen;
	vcamera	*vcamera;
};

static GPPort *port = NULL;

uint8_t socket_init_resp[] = {0x44, 0x0, 0x0, 0x0, 0x2, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x8, 0x70, 0xb0, 0x61, 0xa, 0x8b, 0x45, 0x93,
	0xb2, 0xe7, 0x93, 0x57, 0xdd, 0x36, 0xe0, 0x50, 0x58, 0x0, 0x2d, 0x0, 0x41, 0x0, 0x32, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, };

int ptpip_connection_init() {
	printf("Allocated vusb connection\n");
	port = malloc(sizeof(GPPort));
	C_MEM (port->pl = calloc (1, sizeof (GPPortPrivateLibrary)));
	port->pl->vcamera = vcamera_new(FUJI_X_A2);
	port->pl->vcamera->init(port->pl->vcamera);

	if (port->pl->isopen)
		return -1;

	port->pl->vcamera->open(port->pl->vcamera, port->settings.usb.port);
	port->pl->isopen = 1;
	return 0;
}

int ptpip_cmd_write(void *to, int length) {
	static int first_write = 1;

	if (first_write) {
		struct FujiInitPacket *p = (struct FujiInitPacket *)to;
		printf("vusb: init socket\n");
		first_write = 0;

		ptpip_connection_init();

		// Pretend like we read the packet	
		return length;
	}

	C_PARAMS (port && port->pl && port->pl->vcamera);
	int rc = port->pl->vcamera->write(port->pl->vcamera, 0x02, (unsigned char *)to, length);
	printf("<- read %d (%X)\n", rc, ((uint16_t *)to)[3]);
	return rc;
}

int ptpip_cmd_read(void *to, int length) {
	static int left_of_init_packet = sizeof(socket_init_resp);

	if (left_of_init_packet) {
		memcpy(to, socket_init_resp + sizeof(socket_init_resp) - left_of_init_packet, length);
		left_of_init_packet -= length;
		return length;
	}

	C_PARAMS (port && port->pl && port->pl->vcamera);
	int rc = port->pl->vcamera->read(port->pl->vcamera, 0x81, (unsigned char *)to, length);
	printf("-> write %d (%X)\n", rc, ((uint16_t *)to)[3]);

	return rc;
}

int tcp_recieve_all(int clientSocket) {
	// Read packet length (from the app, which is the initiator)
	uint32_t packet_length;
	ssize_t size = recv(clientSocket, &packet_length, sizeof(uint32_t), 0);
	if (size < 0) {
		perror("Error reading data from socket");
		return -1;
	}

	if (size != 4) {
		printf("Couldn't read length\n");
		return -1;
	}

	// Allocate the rest of the packet to read
	uint8_t *buffer = malloc(size + packet_length);
	((uint32_t *)buffer)[0] = packet_length;

	// Continue reading the rest of the data
	size += recv(clientSocket, buffer + size, packet_length - 4, 0);
	if (size < 0) {
		perror("Error reading data from socket");
		return -1;
	} else if (size != packet_length) {
		printf("Couldn't read the rest of the packet\n");
		return -1;
	}

	// Route the read data into the vcamera. The camera is the responder,
	// and will be the first to write data to the app.
	int rc = ptpip_cmd_write(buffer, size);

	if (rc != size) {
		return -1;
	}

	// Detect data phase from vcam
	struct PtpBulkContainer *c = (struct PtpBulkContainer *)buffer;
	if (port->pl->vcamera->nrinbulk == 0 && c->code != 0x0) {
		printf("Doing data phase response\n");
		free(buffer);

		size = recv(clientSocket, &packet_length, sizeof(uint32_t), 0);
		if (size != sizeof(uint32_t)) {
			printf("Failed to recieve 4 bytes of data phase response\n");
			return -1;
		}

		// Same trick from the recv part
		buffer = malloc(size + packet_length);
		((uint32_t *)buffer)[0] = packet_length;
		rc = recv(clientSocket, buffer + size, packet_length - size, 0);
		if (rc != packet_length - size) {
			printf("Failed to recieve data phase response\n");
			return -1;
		}

		if (rc != packet_length - size) {
			printf("Wrote %d, wanted %d\n", rc, packet_length - size);
			return -1;
		}

		rc = ptpip_cmd_write(buffer, packet_length);
		if (rc != packet_length) {
			printf("Failed to send response to vcam\n");
			return -1;
		}
	}

	free(buffer); 

	return 0;   
}

int tcp_send_all(int clientSocket) {
	uint32_t packet_length = 0;
	int size = ptpip_cmd_read(&packet_length, 4);
	if (size != 4) {
		printf("vcam failed to provide 4 bytes\n", size);
		return -1;
	}

	// Same trick from the recv part
	char *buffer = malloc(size + packet_length);
	((uint32_t *)buffer)[0] = packet_length;
	int rc = ptpip_cmd_read(buffer + size, packet_length - size);

	if (rc != packet_length - size) {
		printf("Read %d, wanted %d\n", rc, packet_length - size);
		return -1;
	}

	// And finally send our response to the initiator
	size = send(clientSocket, buffer, packet_length, 0);
	if (size <= 0) {
		perror("Error sending data to client");
		return -1;
	}

	// As per spec, data phase must have a 12 byte packet following
	struct PtpBulkContainer *c = (struct PtpBulkContainer *)buffer;
	if (c->type == PTP_PACKET_TYPE_DATA && c->code != 0x0) {
		free(buffer);

		// Read packet length
		size = ptpip_cmd_read(&packet_length, 4);
		if (size != 4) {
			printf("vcam failed to provide 4 bytes\n", size);
			return -1;
		}

		// Same trick from the recv part
		buffer = malloc(size + packet_length);
		((uint32_t *)buffer)[0] = packet_length;
		rc = ptpip_cmd_read(buffer + size, packet_length - size);

		if (rc != packet_length - size) {
			printf("Read %d, wanted %d\n", rc, packet_length - size);
			return -1;
		}

		// And finally send our response to the initiator
		size = send(clientSocket, buffer, packet_length, 0);
		if (size <= 0) {
			perror("Error sending data to client");
			return -1;
		}
	}

	return 0;
}

int main() {
	int serverSocket = socket(AF_INET, SOCK_STREAM, 0);

	if (serverSocket == -1) {
		perror("Socket creation failed");
		exit(EXIT_FAILURE);
	}

	struct sockaddr_in serverAddress;
	memset(&serverAddress, 0, sizeof(serverAddress));
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_addr.s_addr = INADDR_ANY;
	serverAddress.sin_port = htons(55740);

	if (bind(serverSocket, (struct sockaddr *)&serverAddress, sizeof(serverAddress)) == -1) {
		perror("Binding failed");
		close(serverSocket);
		return -1;
	}

	if (listen(serverSocket, 5) == -1) {
		perror("Listening failed");
		close(serverSocket);
		return -1;
	}

	printf("Server listening on port 55740...\n");

	struct sockaddr_in clientAddress;
	socklen_t clientAddressLength = sizeof(clientAddress);
	int clientSocket = accept(serverSocket, (struct sockaddr *)&clientAddress, &clientAddressLength);

	if (clientSocket == -1) {
		perror("Accept failed");
		close(serverSocket);
		return -1;
	}

	printf("Connection accepted from %s:%d\n", inet_ntoa(clientAddress.sin_addr), ntohs(clientAddress.sin_port));

	while (1) {
		if (tcp_recieve_all(clientSocket)) {
			goto err;
		}
	
		// Now the app has sent the data, and is waiting for a response.

		// Read packet length
		if (tcp_send_all(clientSocket)) {
			goto err;
		}
	}

	close(clientSocket);
	printf("Connection closed\n");
	close(serverSocket);

	return 0;

	err:;
	puts("Connection forced down");
	close(clientSocket);
	close(serverSocket);
	return -1;
}
