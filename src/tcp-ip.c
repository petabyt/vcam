// PTP/IP packet wrapper over vusb USB packets - emulates standard PTP/IP as per spec
// Mostly similar to Fuji TCP code, except this uses PTP/IP style packets
// This code will listen on *any* IP address, so it will open up the ISO standard PTP port
// to the entire computer
#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <vcam.h>

//#define TCP_NOISY

void *conv_ip_cmd_packet_to_usb(char *buffer, int length, int *outlength);
void *conv_usb_packet_to_ip(char *buffer, int length, int *outlength);
void *conv_ip_data_packets_to_usb(void *ds_buffer, void *de_buffer, int *outlength, int opcode);

struct _GPPortPrivateLibrary {
	int isopen;
	vcamera *vcamera;
};

static GPPort *port = NULL;

// TODO: Convert to structure
static uint8_t socket_init_resp[] = {
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

static int ptpip_connection_init(struct CamConfig *options) {
	vcam_log("Allocated vusb connection\n");
	port = malloc(sizeof(GPPort));
	C_MEM(port->pl = calloc(1, sizeof(GPPortPrivateLibrary)));
	port->pl->vcamera = vcamera_new(CAM_CANON);
	port->pl->vcamera->conf = options;
	port->pl->vcamera->init(port->pl->vcamera);

	if (port->pl->isopen)
		return -1;

	port->pl->vcamera->open(port->pl->vcamera, port->settings.usb.port);
	port->pl->isopen = 1;
	return 0;
}

static int ptpip_cmd_write(void *to, int length) {
	static int first_write = 1;

	// First packet from the app, info about device
	if (first_write) {
		struct FujiInitPacket *p = (struct FujiInitPacket *)to;
		vcam_log("vusb: init socket (%d bytes)\n", length);

		first_write = 0;

		// Pretend like we read the packet
		return length;
	}

	C_PARAMS(port && port->pl && port->pl->vcamera);
	int rc = port->pl->vcamera->write(port->pl->vcamera, 0x02, (unsigned char *)to, length);

	#ifdef TCP_NOISY
	vcam_log("<- read %d (%X)\n", rc, ((uint16_t *)to)[3]);
	#endif

	return rc;
}

static int ptpip_cmd_read(void *to, int length) {
	C_PARAMS(port && port->pl && port->pl->vcamera);
	int rc = port->pl->vcamera->read(port->pl->vcamera, 0x81, (unsigned char *)to, length);

	#ifdef TCP_NOISY
	vcam_log("-> write %d (%X)\n", rc, ((uint16_t *)to)[3]);
	#endif

	return rc;
}

static void *tcp_recieve_single_packet(int client_socket, int *length) {
	// Read packet length (from the app, which is the initiator)
	uint32_t packet_length;
	ssize_t size;
	for (int i = 0; i < 10; i++) {
		size = recv(client_socket, &packet_length, sizeof(uint32_t), 0);

		#ifdef TCP_NOISY
		vcam_log("Read %d\n", size);
		#endif

		if (size == 0) {
			vcam_log("Initiator isn't sending anything, trying again\n");
			usleep(1000 * 500); // 1s
			continue;
		}

		if (size < 0) {
			perror("Error reading data from socket");
			return NULL;
		}

		if (size != 4) {
			vcam_log("Couldn't read 4 bytes, only got %d\n", size);
			return NULL;
		}

		break;
	}

	// Allocate the rest of the packet to read
	uint8_t *buffer = malloc(size + packet_length);
	((uint32_t *)buffer)[0] = packet_length;

	// Continue reading the rest of the data
	size += recv(client_socket, buffer + size, packet_length - size, 0);
#ifdef TCP_NOISY
	vcam_log("Read %d\n", size);
#endif

	(*length) = size;

	if (size < 0) {
		perror("Error reading data from socket");
		return NULL;
	} else if (size != packet_length) {
		vcam_log("Couldn't read the rest of the packet, only got %d\n", size);
		return NULL;
	}

	return buffer;
}

// Recieve data from app TCP, and route into vcam
static int tcp_recieve_all(int client_socket) {
	int packet_length = 0;
	void *buffer = tcp_recieve_single_packet(client_socket, &packet_length);
	if (buffer == NULL) {
		return -1;
	}

	struct PtpIpBulkContainer *bc = (struct PtpIpBulkContainer *)buffer;
	if (bc->type == PTPIP_COMMAND_REQUEST) {
		void *new_buffer = conv_ip_cmd_packet_to_usb(buffer, packet_length, &packet_length);

		// Route the read data into the vcamera. The camera is the responder,
		// and will next be writing data to the app.
		int rc = ptpip_cmd_write(new_buffer, packet_length);
		if (rc != packet_length) {
			return -1;
		}

		free(new_buffer);
	} else if (bc->type == PTPIP_INIT_COMMAND_REQ) {
		// Recieved init packet, send it into vcam to init vcam structs
		vcam_log("Recieved init packet\n");
		ptpip_cmd_write(buffer, packet_length);
		return 0;
	}

	if (bc->data_phase == 2) {
		vcam_log("Recieved data phase\n");

		// Read in data start packet
		void *buffer_ds = tcp_recieve_single_packet(client_socket, &packet_length);
		if (buffer_ds == NULL) {
			return -1;
		}

		struct PtpIpStartDataPacket *ds = (struct PtpIpStartDataPacket *)buffer_ds;
		if (ds->type != PTPIP_DATA_PACKET_START) {
			vcam_log("Didn't get end data packet\n");
			return -1;
		}

		// Recieve data end packet (which has payload)
		void *buffer_de = tcp_recieve_single_packet(client_socket, &packet_length);
		if (buffer_de == NULL) {
			return -1;
		}

		struct PtpIpEndDataPacket *ed = (struct PtpIpEndDataPacket *)buffer_de;
		if (ed->type != PTPIP_DATA_PACKET_END) {
			vcam_log("Didn't get end data packet\n");
			return -1;
		}

		void *new_buffer = conv_ip_data_packets_to_usb(buffer_ds, buffer_de, &packet_length, bc->code);
		free(buffer_ds);
		free(buffer_de);

		// Finally, send the single packet into vcam
		int rc = ptpip_cmd_write(new_buffer, packet_length);
		if (rc != packet_length) {
			return -1;
		}

		free(new_buffer);
	}

	free(buffer);

	return 0;
}

static void *ptpip_cmd_read_single_packet(int *length) {
	uint32_t packet_length = 0;
	int size = ptpip_cmd_read(&packet_length, 4);
	if (size != 4) {
		vcam_log("send_all: vcam failed to provide 4 bytes: %d\n", size);
		return NULL;
	}

	// Yes, this allocates 4 bytes too much, can't be bothered to fix it
	char *buffer = malloc(size + packet_length);
	((uint32_t *)buffer)[0] = packet_length;
	int rc = ptpip_cmd_read(buffer + size, packet_length - size);	

	if (rc != packet_length - size) {
		vcam_log("Read %d, wanted %d\n", rc, packet_length - size);
		return NULL;
	}

	if (length != NULL) {
		(*length) = packet_length;
	}

	return buffer;
}

static int tcp_send_all(int client_socket) {
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

static int new_ptp_tcp_socket(int port) {
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

	vcam_log("Socket listening on port %d...\n", port);

	return server_socket;
}

static int ack_event_socket(int client_event_socket) {
	struct PtpIpHeader init;
	ssize_t size = recv(client_event_socket, &init, 12, 0);
	if (size != 12) {
		vcam_log("Failed to read socket init: %d\n", size);
		return -1;
	}

	vcam_log("Recieved event socket req\n");

	uint32_t ack[2] = {
		8, // size
		PTPIP_INIT_EVENT_ACK
	};

	size = send(client_event_socket, ack, sizeof(ack), 0);	
	if (size != sizeof(ack)) {
		vcam_log("Failed to send socket ack\n");
		return -1;
	}

	vcam_log("Send event socket ack\n");

	return 0;
}

static void *bind_event_socket_thread(void *arg) {
	struct sockaddr_in client_address;
	socklen_t client_address_length = sizeof(client_address);
	int server_socket = *((int *)arg);
	vcam_log("Waiting for event socket on new thread...\n");
	int client_event_socket = accept(server_socket, (struct sockaddr *)&client_address, &client_address_length);
	if (ack_event_socket(client_event_socket)) {
		return NULL;
	}

	vcam_log("Event socket accepted from %s:%d\n", inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port));

	// Canon devices don't seem to send any events
	while (1) {
		vcam_log("Event thread sleeping\n");
		usleep(1000 * 1000);
	}

	return NULL;
}

int canon_wifi_main(struct CamConfig *options) {
	printf("Canon vcam - running %s\n", options->model);

	ptpip_connection_init(options);

	int server_socket = new_ptp_tcp_socket(PTP_IP_PORT);

	struct sockaddr_in client_address;
	socklen_t client_address_length = sizeof(client_address);
	int client_socket = accept(server_socket, (struct sockaddr *)&client_address, &client_address_length);

	if (client_socket == -1) {
		perror("Accept failed");
		close(server_socket);
		return -1;
	}

	vcam_log("Connection accepted from %s:%d\n", inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port));

	static int have_setup_events_socket = 0;
	pthread_t thread;
	while (1) {
		if (tcp_recieve_all(client_socket)) {
			vcam_log("tcp_recieve_all failed\n");
			goto err;
		}

		// Now the app has sent the data, and is waiting for a response.

		// Read packet length
		if (tcp_send_all(client_socket)) {
			vcam_log("tcp_send_all failed\n");
			goto err;
		}

		// Once the first packets are sent, start by opening the event socket and listening on another thread
		if (!have_setup_events_socket) {
			if (pthread_create(&thread, NULL, bind_event_socket_thread, &server_socket)) {
				return 1;
			}

			have_setup_events_socket = 1;
		}
	}

	close(client_socket);
	vcam_log("Connection closed\n");
	close(server_socket);

	return 0;

err:;
	vcam_log("Connection forced down\n");
	close(client_socket);
	close(server_socket);
	return -1;
}
