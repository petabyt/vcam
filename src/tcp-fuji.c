// Fujifilm PTP/IP/USB TCP I/O interface
// For X cameras 2014-2017
// Copyright Daniel C - GNU Lesser General Public License v2.1
#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <vcam.h>
#include <fujiptp.h>

static const char *server_ip_address = "192.168.0.1";

int fuji_open_remote_port = 0; // TODO: Move to int in vcamera

struct _GPPortPrivateLibrary {
	int isopen;
	vcamera *vcamera;
};

static GPPort *port = NULL;

static int ptpip_cmd_write(void *to, int length) {
	static int first_write = 1;

	if (first_write) {
		vcam_log("vusb: got first write: %d\n", length);
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
	static int left_of_init_packet = FUJI_ACK_PACKET_SIZE;

	uint8_t *packet = fuji_get_ack_packet(port->pl->vcamera);

	if (left_of_init_packet) {
		memcpy(to, packet + FUJI_ACK_PACKET_SIZE - left_of_init_packet, length);
		left_of_init_packet -= length;
		return length;
	}

	C_PARAMS(port && port->pl && port->pl->vcamera);
	int rc = port->pl->vcamera->read(port->pl->vcamera, 0x81, (unsigned char *)to, length);
#ifdef TCP_NOISY
	vcam_log("-> write %d (%X)\n", rc, ((uint16_t *)to)[3]);
#endif
	return rc;
}

// Recieve all packets from the app (initiator)
static int tcp_recieve_all(int client_socket) {
	uint32_t packet_length;
	ssize_t size;
	for (int i = 0; i < 10; i++) {
		size = recv(client_socket, &packet_length, sizeof(uint32_t), 0);

#ifdef TCP_NOISY
		vcam_log("Read %d\n", size);
#endif

		if (size == 0) {
			vcam_log("Initiator isn't sending anything, trying again\n");
			usleep(1000 * 500);
			continue;
		}

		if (size < 0) {
			perror("Error reading data from socket");
			return -1;
		}

		if (size != 4) {
			vcam_log("Couldn't read 4 bytes, only got %d: %X\n", size, packet_length);

			size += recv(client_socket, (uint8_t *)(&packet_length) + size, sizeof(uint32_t) - size, 0);
			if (size == sizeof(uint32_t)) {
				vcam_log("Acting up, didn't send all of size at first: %X\n", packet_length);
				break;
			}
			
			return -1;
		}

		break;
	}

	// Allocate the rest of the packet to read
	uint8_t *buffer = malloc(size + packet_length);
	((uint32_t *)buffer)[0] = packet_length;

	// Continue reading the rest of the data
	size += recv(client_socket, buffer + size, packet_length - 4, 0);

	if (size < 0) {
		perror("Error reading data from socket");
		return -1;
	} else if (size != packet_length) {
		vcam_log("Couldn't read the rest of the packet, only got %d out of %d\n", size, packet_length);
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
#ifdef TCP_NOISY
		vcam_log("Doing data phase response\n");
#endif
		free(buffer);

		size = recv(client_socket, &packet_length, sizeof(uint32_t), 0);
		if (size != sizeof(uint32_t)) {
			vcam_log("Failed to recieve 4 bytes of data phase response\n");
			return -1;
		}

		// Same trick from the recv part
		buffer = malloc(size + packet_length);
		((uint32_t *)buffer)[0] = packet_length;
		rc = recv(client_socket, buffer + size, packet_length - size, 0);
		if (rc != packet_length - size) {
			vcam_log("Failed to recieve data phase response\n");
			return -1;
		}

		if (rc != packet_length - size) {
			vcam_log("Wrote %d, wanted %d\n", rc, packet_length - size);
			return -1;
		}

		rc = ptpip_cmd_write(buffer, packet_length);
		if (rc != packet_length) {
			vcam_log("Failed to send response to vcam\n");
			return -1;
		}
	}

	free(buffer);

	return 0;
}

static int tcp_send_all(int client_socket) {
	uint32_t packet_length = 0;
	int size = ptpip_cmd_read(&packet_length, 4);
	if (size != 4) {
		vcam_log("send_all: vcam failed to provide 4 bytes\n", size);
		return -1;
	}

	// Same trick from the recv part
	char *buffer = malloc(size + packet_length);
	((uint32_t *)buffer)[0] = packet_length;
	int rc = ptpip_cmd_read(buffer + size, packet_length - size);

	if (rc != packet_length - size) {
		vcam_log("Read %d, wanted %d\n", rc, packet_length - size);
		return -1;
	}

	// And finally send our response to the initiator
	size = send(client_socket, buffer, packet_length, 0);
	if (size <= 0) {
		perror("Error sending data to client");
		return -1;
	}

	// As per spec, data phase must have a 12 byte packet following
	struct PtpBulkContainer *c = (struct PtpBulkContainer *)buffer;
	if (c->type == PTP_PACKET_TYPE_DATA && c->code != 0x0) {

		// Read packet length
		size = ptpip_cmd_read(&packet_length, 4);
		if (size != 4) {
			vcam_log("response packet: vcam failed to provide 4 bytes: %d\n", size);
			vcam_log("Code: %X\n", c->code);
			return -1;
		}

		// Same trick from the recv part
		buffer = malloc(size + packet_length);
		((uint32_t *)buffer)[0] = packet_length;
		rc = ptpip_cmd_read(buffer + size, packet_length - size);

		if (rc != packet_length - size) {
			vcam_log("Read %d, wanted %d\n", rc, packet_length - size);
			return -1;
		}

		// And finally send our response to the initiator
		size = send(client_socket, buffer, packet_length, 0);
		if (size <= 0) {
			perror("Error sending data to client");
			return -1;
		}
	}

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
	serverAddress.sin_addr.s_addr = inet_addr(server_ip_address);
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

	printf("Socket listening on %s:%d...\n", server_ip_address, port);

	return server_socket;
}

int ptp_fuji_liveview(int socket);

static void *fuji_accept_remote_ports_thread(void *arg) {
	int event_socket = new_ptp_tcp_socket(FUJI_EVENT_IP_PORT);
	int video_socket = new_ptp_tcp_socket(FUJI_LIVEVIEW_IP_PORT);

	struct sockaddr_in client_address_event;
	socklen_t client_address_length_event = sizeof(client_address_event);
	int client_socket_event = accept(event_socket, (struct sockaddr *)&client_address_event, &client_address_length_event);

	printf("Event port connection accepted from %s:%d\n", inet_ntoa(client_address_event.sin_addr), ntohs(client_address_event.sin_port));

	struct sockaddr_in client_address_video;
	socklen_t client_address_length_video = sizeof(client_address_video);
	int client_socket_video = accept(video_socket, (struct sockaddr *)&client_address_video, &client_address_length_video);

	printf("Video port connection accepted from %s:%d\n", inet_ntoa(client_address_video.sin_addr), ntohs(client_address_video.sin_port));

	ptp_fuji_liveview(client_socket_video);

	while (1) {
		printf("Liveview thread sleeping...\n");
		usleep(1000000);
	}

	return (void *)0;
}

static void fuji_accept_remote_ports() {
	pthread_t thread;

	if (pthread_create(&thread, NULL, fuji_accept_remote_ports_thread, NULL)) {
		return;
	}

	printf("Started new thread to accept remote ports\n");
}

static int init_vcam(struct CamConfig *options) {
	port = malloc(sizeof(GPPort));
	C_MEM(port->pl = calloc(1, sizeof(GPPortPrivateLibrary)));
	port->pl->vcamera = vcamera_new(CAM_FUJI_WIFI);
	port->pl->vcamera->conf = options;
	port->pl->vcamera->init(port->pl->vcamera);

	if (port->pl->isopen)
		return -1;

	port->pl->vcamera->open(port->pl->vcamera, port->settings.usb.port);
	port->pl->isopen = 1;
}

int fuji_wifi_main(struct CamConfig *options) {
	printf("Fuji vcam - running '%s'\n", options->model);

	init_vcam(options);

	if (options->use_local) {
		server_ip_address = "192.168.1.33";
	}

	int server_socket = new_ptp_tcp_socket(FUJI_CMD_IP_PORT);
	if (server_socket == -1) {
		printf("Error, make sure to add virtual network device\n");
		return 1;
	}

	struct sockaddr_in client_address;
	socklen_t client_address_length = sizeof(client_address);
	int client_socket = accept(server_socket, (struct sockaddr *)&client_address, &client_address_length);

	if (client_socket == -1) {
		perror("Accept failed");
		close(server_socket);
		return -1;
	}

	printf("Connection accepted from %s:%d\n", inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port));

	while (1) {
		if (tcp_recieve_all(client_socket)) {
			goto err;
		}

		// Now the app has sent the data, and is waiting for a response.

		// Read packet length
		if (tcp_send_all(client_socket)) {
			goto err;
		}

		if (fuji_open_remote_port == 1) {
			fuji_accept_remote_ports();
			fuji_open_remote_port++;
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
