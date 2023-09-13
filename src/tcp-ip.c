// PTP/IP packet wrapper over vusb USB packets - emulates standard PTP/IP as per spec
#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <ptp.h>

#define _GPHOTO2_INTERNAL_CODE
#define _DARWIN_C_SOURCE
#include <config.h>
#include <gphoto2/gphoto2-port-library.h>
#include <gphoto2/gphoto2-port-log.h>
#include <gphoto2/gphoto2-port-result.h>
#include <gphoto2/gphoto2-port.h>
#include <libgphoto2_port/i18n.h>
#include <vcamera.h>

#define TCP_NOISY

void *conv_ip_cmd_packet_to_usb(char *buffer, int length, int *outlength);
void *conv_usb_packet_to_ip(char *buffer, int length, int *outlength);

struct _GPPortPrivateLibrary {
	int isopen;
	vcamera *vcamera;
};

static GPPort *port = NULL;

uint8_t socket_init_resp[] = {
0x2e, 0x0, 0x0, 0x0,
0x2, 0x0, 0x0, 0x0,
0x1, 0x0, 0x0, 0x0,
0x0, 0x0, 0x0, 0x0,
0x0, 0x0, 0x0, 0x0,
0x0, 0x1, 0x0, 0xbb,
0xc1, 0x85, 0x9f, 0xab,
0x45, 0x0, 0x4f, 0x0, 0x53, 0x0, 0x54, 0x0, 0x36, 0x0, 0x7b, 0x0, 0x0, 0x0,
0x0, 0x0,
0x1, 0x0,};

int ptpip_connection_init() {
	printf("Allocated vusb connection\n");
	port = malloc(sizeof(GPPort));
	C_MEM(port->pl = calloc(1, sizeof(GPPortPrivateLibrary)));
	port->pl->vcamera = vcamera_new(CANON_1300D);
	port->pl->vcamera->init(port->pl->vcamera);

	if (port->pl->isopen)
		return -1;

	port->pl->vcamera->open(port->pl->vcamera, port->settings.usb.port);
	port->pl->isopen = 1;
	return 0;
}

int ptpip_cmd_write(void *to, int length) {
	static int first_write = 1;

	// First packet from the app, info about device
	if (first_write) {
		struct FujiInitPacket *p = (struct FujiInitPacket *)to;
		printf("vusb: init socket (%d bytes)\n", length);

		// Too lazy to decode struct
		for (int i = 0; i < length; i++) {
			printf("%c", ((char *)to)[i]);
		}
		puts("");

		first_write = 0;
		ptpip_connection_init();

		// Pretend like we read the packet
		return length;
	}

	C_PARAMS(port && port->pl && port->pl->vcamera);
	int rc = port->pl->vcamera->write(port->pl->vcamera, 0x02, (unsigned char *)to, length);
#ifdef TCP_NOISY
	printf("<- read %d (%X)\n", rc, ((uint16_t *)to)[3]);
#endif
	return rc;
}

int ptpip_cmd_read(void *to, int length) {
	C_PARAMS(port && port->pl && port->pl->vcamera);
	int rc = port->pl->vcamera->read(port->pl->vcamera, 0x81, (unsigned char *)to, length);
#ifdef TCP_NOISY
	printf("-> write %d (%X)\n", rc, ((uint16_t *)to)[3]);
#endif
	return rc;
}

void *tcp_recieve_single_packet(int client_socket, int *length) {
	// Read packet length (from the app, which is the initiator)
	uint32_t packet_length;
	ssize_t size = recv(client_socket, &packet_length, sizeof(uint32_t), 0);
#ifdef TCP_NOISY
	printf("Read %d\n", size);
#endif

	if (size < 0) {
		perror("Error reading data from socket");
		return NULL;
	}

	if (size != 4) {
		printf("Couldn't read 4 bytes, only got %d\n", size);
		return NULL;
	}

	// Allocate the rest of the packet to read
	uint8_t *buffer = malloc(size + packet_length);
	((uint32_t *)buffer)[0] = packet_length;

	// Continue reading the rest of the data
	size += recv(client_socket, buffer + size, packet_length - size, 0);
#ifdef TCP_NOISY
	printf("Read %d\n", size);
#endif

	(*length) = size;

	if (size < 0) {
		perror("Error reading data from socket");
		return NULL;
	} else if (size != packet_length) {
		printf("Couldn't read the rest of the packet, only got %d\n", size);
		return NULL;
	}

	return buffer;
}

// Recieve data from app TCP, and route into vcam
int tcp_recieve_all(int client_socket) {
	int packet_length = 0;
	void *buffer = tcp_recieve_single_packet(client_socket, &packet_length);
	if (buffer == NULL) {
		return -1;
	}

	struct PtpIpBulkContainer *bc = (struct PtpIpBulkContainer *)buffer;
	if (bc->type == PTPIP_COMMAND_REQUEST) {
		void *new_buffer = conv_ip_cmd_packet_to_usb(buffer, packet_length, &packet_length);

		//printf("Processing cmd req for %X\n", bc->code);

		printf("Length %d\n", packet_length);

		// Route the read data into the vcamera. The camera is the responder,
		// and will be the first to write data to the app.
		int rc = ptpip_cmd_write(new_buffer, packet_length);
		if (rc != packet_length) {
			return -1;
		}

		free(new_buffer);
	} else if (bc->type == PTPIP_INIT_COMMAND_REQ) {
		// Recieved init packet, send it into vcam to init vcam structs
		puts("Recieved init packet");
		ptpip_cmd_write(buffer, packet_length);
		return 0;
	} else {
		// We shouldn't get anything else
	}

	if (bc->data_phase == 1) {
		printf("No data phase\n");
	} else {
		printf("Recieved data phase\n");
		return -1;
	}

	free(buffer);

	return 0;

	// TODO: Need to get command packet, data start packet, and data end packet
	// Would require a seperate funtion to recieve a single packet, rather than doing packet_length
	// and malloc() nonsense over and over again.

	// Detect if vcam is expecting on recieving a data phase
	//struct PtpBulkContainer *c = (struct PtpBulkContainer *)buffer;
	// if (port->pl->vcamera->nrinbulk == 0) {
// #ifdef TCP_NOISY
		// printf("Doing data phase response\n");
// #endif
		// free(buffer);
// 
		// // Get data start and end packets
// 
		// void *sd_buffer = tcp_recieve_single_packet(&packet_length);
		// if (buffer == NULL) {
			// return -1;
		// }
// 
		// struct PtpIpBulkContainer *bc = (struct PtpIpBulkContainer *)buffer;
// 
		// rc = ptpip_cmd_write(buffer, packet_length);
		// if (rc != packet_length) {
			// printf("Failed to send response to vcam\n");
			// return -1;
		// }
	// }
// 
	// free(buffer);

	return 0;
}

void *ptpip_cmd_read_single_packet(int *length) {
	uint32_t packet_length = 0;
	int size = ptpip_cmd_read(&packet_length, 4);
	if (size != 4) {
		printf("send_all: vcam failed to provide 4 bytes: %d\n", size);
		return NULL;
	}

	// Yes, this allocates 4 bytes too much, can't be bothered to fix it
	char *buffer = malloc(size + packet_length);
	((uint32_t *)buffer)[0] = packet_length;
	int rc = ptpip_cmd_read(buffer + size, packet_length - size);	

	if (rc != packet_length - size) {
		printf("Read %d, wanted %d\n", rc, packet_length - size);
		return NULL;
	}

	if (length != NULL) {
		(*length) = packet_length;
	}

	return buffer;
}

int tcp_send_all(int client_socket) {
	static int left_of_init_packet = sizeof(socket_init_resp);

	// Wait until 'hello' packet is sent
	if (left_of_init_packet) {
		int size = send(client_socket, socket_init_resp, left_of_init_packet, 0);
		if (size <= 0) {
			perror("Error sending data to client");
			return -1;
		}

		left_of_init_packet -= size;
		return 0;
	}

	int packet_length = 0;
	void *buffer = ptpip_cmd_read_single_packet(&packet_length);
	if (buffer == NULL) {
		return -1;
	}

	void *new_buffer = conv_usb_packet_to_ip(buffer, packet_length, &packet_length);

	printf("len %d\n", packet_length);

	// Send our response to the initiator
	int size = send(client_socket, new_buffer, packet_length, 0);
	if (size <= 0) {
		perror("Error sending data to client");
		return -1;
	}

	free(new_buffer);

	// As per spec, data phase must have a 12 byte packet following
	struct PtpBulkContainer *c = (struct PtpBulkContainer *)buffer;
	if (c->type == PTP_PACKET_TYPE_DATA && c->code != 0x0) {

		// TODO: If vcam returns data phase, then command -> data start -> data end packet must follow
		// TODO: Create these artifically

		free(buffer);
		buffer = ptpip_cmd_read_single_packet(&packet_length);
		if (buffer == NULL) {
			return -1;
		}

		new_buffer = conv_usb_packet_to_ip(buffer, packet_length, &packet_length);

		// And finally send our response to the initiator
		size = send(client_socket, new_buffer, packet_length, 0);
		if (size <= 0) {
			perror("Error sending data to client");
			return -1;
		}

		free(new_buffer);
	}

	free(buffer);

	return 0;
}

int new_ptp_tcp_socket(int port) {
	int server_socket = socket(AF_INET, SOCK_STREAM, 0);

	if (server_socket == -1) {
		perror("Socket creation failed");
		exit(EXIT_FAILURE);
	}

	int true = 1;
	if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &true, sizeof(int)) < 0) {
		perror("Failed to set sockopt");
	}

	struct sockaddr_in serverAddress;
	memset(&serverAddress, 0, sizeof(serverAddress));
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_addr.s_addr = INADDR_ANY;
	serverAddress.sin_port = htons(port);

	if (bind(server_socket, (struct sockaddr *)&serverAddress, sizeof(serverAddress)) == -1) {
		perror("Binding failed");
		close(server_socket);
		return -1;
	}

	if (listen(server_socket, 5) == -1) {
		perror("Listening failed");
		close(server_socket);
		return -1;
	}

	printf("Socket listening on port %d...\n", port);

	return server_socket;
}

static int ack_event_socket(int client_event_socket) {
	struct PtpIpHeader init;
	ssize_t size = recv(client_event_socket, &init, 12, 0);
	if (size != 12) {
		printf("Failed to read socket init: %d\n", size);
		return -1;
	}

	uint32_t ack[2] = {0, 0};

	size = send(client_event_socket, ack, sizeof(ack), 0);	
	if (size != sizeof(ack)) {
		printf("Failed to send socket ack\n");
		return -1;
	}

	return 0;
}

int main() {
	int server_socket = new_ptp_tcp_socket(PTP_IP_PORT);

	struct sockaddr_in client_address;
	socklen_t client_address_length = sizeof(client_address);
	int client_socket = accept(server_socket, (struct sockaddr *)&client_address, &client_address_length);

	if (client_socket == -1) {
		perror("Accept failed");
		close(server_socket);
		return -1;
	}

	printf("Connection accepted from %s:%d\n", inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port));

	static int have_setup_events_socket = 0;

	while (1) {
		if (tcp_recieve_all(client_socket)) {
			puts("tcp_recieve_all failed");
			goto err;
		}

		// Now the app has sent the data, and is waiting for a response.

		// Read packet length
		if (tcp_send_all(client_socket)) {
			puts("tcp_send_all failed");
			goto err;
		}

		if (!have_setup_events_socket) {
			puts("Waiting for event socket");
			int client_event_socket = accept(server_socket, (struct sockaddr *)&client_address, &client_address_length);
			if (ack_event_socket(client_event_socket)) {
				goto err;
			}
			have_setup_events_socket = 1;
		}
	}

	close(client_socket);
	printf("Connection closed\n");
	close(server_socket);

	return 0;

err:;
	puts("Connection forced down");
	close(client_socket);
	close(server_socket);
	return -1;
}
