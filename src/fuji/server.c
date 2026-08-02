// Fujifilm PTP/IP/USB TCP I/O interface
// For X cameras 2014-2017
// Copyright Daniel C - GNU Lesser General Public License v2.1
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <signal.h>
#include <vcam.h>
#include <fujiptp.h>
#include "fuji.h"

static const char *server_ip_address = "192.168.0.1";
static int first_client_write = 1;
static int left_of_init_packet = FUJI_ACK_PACKET_SIZE;

static void format_sockaddr(const struct sockaddr *addr, socklen_t addrlen, char *host, size_t host_len, char *service, size_t service_len) {
	int rc = getnameinfo(addr, addrlen, host, host_len, service, service_len, NI_NUMERICHOST | NI_NUMERICSERV);
	if (rc) {
		snprintf(host, host_len, "<unknown>");
		snprintf(service, service_len, "0");
	}
}

static void reset_client_bridge(vcam *cam) {
	first_client_write = 1;
	left_of_init_packet = FUJI_ACK_PACKET_SIZE;
	cam->session = 0;
	cam->seqnr = 0;
	cam->nrinbulk = 0;
	cam->nroutbulk = 0;
	fuji_reset_image_import_state(cam);
}

static int recv_exact(int socket, void *buffer, size_t length) {
	size_t off = 0;
	while (off < length) {
		ssize_t rc = recv(socket, (unsigned char *)buffer + off, length - off, 0);
		if (rc == 0) {
			return off ? -1 : 0;
		}
		if (rc < 0) {
			if (errno == EINTR) {
				continue;
			}
			return -1;
		}
		off += (size_t)rc;
	}
	return (int)off;
}

static int ptpip_cmd_client_write(vcam *cam, void *to, int length) {
	if (first_client_write) {
		char client_name[100];
		ptp_read_unicode_string(client_name, ((char *)to) + 28, sizeof(client_name));
		vcam_log("Connecting to client '%s'", client_name);
		first_client_write = 0;
		return length;
	}

	int rc = vcam_write(cam, 0x02, (unsigned char *)to, length);
	return rc;
}

static int ptpip_cmd_client_read(vcam *cam, void *to, int length) {
	uint8_t *packet = fuji_get_ack_packet(cam);

	if (left_of_init_packet) {
		memcpy(to, packet + FUJI_ACK_PACKET_SIZE - left_of_init_packet, length);
		left_of_init_packet -= length;
		return length;
	}

	int rc = vcam_read(cam, 0x81, (unsigned char *)to, length);
	return rc;
}

// Receive one packet from the app (initiator). Returns 1 for clean client close.
static int tcp_receive_all(vcam *cam, int client_socket) {
	uint32_t packet_length = 0;
	int size;
	{
		vcam_log("Receiving data from the client...");
		size = recv_exact(client_socket, &packet_length, sizeof(uint32_t));
		if (size == 0) {
			vcam_log("Client closed command socket");
			return 1;
		}
		if (size != sizeof(uint32_t)) {
			vcam_log("Couldn't read packet length from client");
			return -1;
		}
	}

	if (packet_length == 8) {
		uint32_t sentinel = 0;
		size = recv_exact(client_socket, &sentinel, sizeof(uint32_t));
		if (size == sizeof(uint32_t) && sentinel == 0xffffffff) {
			vcam_log("Image-import command-socket close sentinel received");
			return 1;
		}
		vcam_log("Malformed 8-byte packet on command socket: %08x", sentinel);
		return -1;
	}

	if (packet_length < 12 || packet_length > (128 * 1024 * 1024)) {
		vcam_log("Invalid packet length from client: %u", packet_length);
		return -1;
	}

	// Allocate the rest of the packet to read
	uint8_t *buffer = malloc(packet_length);
	if (!buffer) {
		return -1;
	}
	((uint32_t *)buffer)[0] = packet_length;

	// Continue reading the rest of the data
	size = recv_exact(client_socket, buffer + 4, packet_length - 4);
	if (size != (int)(packet_length - 4)) {
		vcam_log("Couldn't read the rest of the packet, only got %d out of %d", size, packet_length - 4);
		free(buffer);
		return -1;
	}
	size = (int)packet_length;

	// Route the read data into the vcam. The camera is the responder,
	// and will be the first to write data to the app.
	int rc = ptpip_cmd_client_write(cam, buffer, size);
	if (rc != size) {
		free(buffer);
		return -1;
	}

	// Detect data phase from vcam
	struct PtpBulkContainer *c = (struct PtpBulkContainer *)buffer;
	if (cam->nrinbulk == 0 && c->code != 0x0) {
		free(buffer);

		size = recv_exact(client_socket, &packet_length, sizeof(uint32_t));
		if (size != sizeof(uint32_t)) {
			vcam_log("Failed to receive 4 bytes of data phase response");
			return -1;
		}

		// Same trick from the recv part
		buffer = malloc(packet_length);
		if (!buffer) {
			return -1;
		}
		((uint32_t *)buffer)[0] = packet_length;
		rc = recv_exact(client_socket, buffer + sizeof(uint32_t), packet_length - sizeof(uint32_t));
		if (rc != (int)(packet_length - sizeof(uint32_t))) {
			vcam_log("Failed to receive data phase response");
			free(buffer);
			return -1;
		}

		rc = ptpip_cmd_client_write(cam, buffer, packet_length);
		if (rc != packet_length) {
			vcam_log("Failed to send response to vcam");
			free(buffer);
			return -1;
		}
	}

	free(buffer);

	return 0;
}

static int tcp_send_all(vcam *cam, int client_socket) {
	uint32_t packet_length = 0;
	int size = ptpip_cmd_client_read(cam, &packet_length, 4);
	if (size != 4) {
		vcam_log("send_all: vcam failed to provide 4 bytes", size);
		return -1;
	}

	// Same trick from the recv part
	char *buffer = malloc(size + packet_length);
	if (!buffer) {
		return -1;
	}
	((uint32_t *)buffer)[0] = packet_length;
	int rc = ptpip_cmd_client_read(cam, buffer + size, packet_length - size);

	if (rc != packet_length - size) {
		vcam_log("Read %d, wanted %d", rc, packet_length - size);
		free(buffer);
		return -1;
	}

	// Send response (or data packet)
	size = send(client_socket, buffer, packet_length, 0);
	if (size <= 0) {
		perror("Error sending data to client");
		free(buffer);
		return -1;
	}

	// As per spec, data phase must have a 12 byte packet following
	struct PtpBulkContainer *c = (struct PtpBulkContainer *)buffer;
	int sent_data_phase = c->type == PTP_PACKET_TYPE_DATA && c->code != 0x0;
	uint16_t sent_code = c->code;
	free(buffer);
	buffer = NULL;
	if (sent_data_phase) {

		// Read packet length
		size = ptpip_cmd_client_read(cam, &packet_length, 4);
		if (size != 4) {
			vcam_log("response packet: vcam failed to provide 4 bytes: %d", size);
			vcam_log("Code: %X", sent_code);
			return -1;
		}

		// Same trick from the recv part
		buffer = malloc(size + packet_length);
		if (!buffer) {
			return -1;
		}
		((uint32_t *)buffer)[0] = packet_length;
		rc = ptpip_cmd_client_read(cam, buffer + size, packet_length - size);

		if (rc != packet_length - size) {
			vcam_log("Read %d, wanted %d", rc, packet_length - size);
			free(buffer);
			return -1;
		}

		// Send our response
		size = send(client_socket, buffer, packet_length, 0);
		if (size <= 0) {
			perror("Error sending data to client");
			free(buffer);
			return -1;
		}
	}

	free(buffer);

	return 0;
}

static int new_ptp_tcp_socket(int port) {
	char service[16];
	snprintf(service, sizeof(service), "%d", port);

	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	struct addrinfo *result = NULL;
	int rc = getaddrinfo(server_ip_address, service, &hints, &result);
	if (rc) {
		vcam_log("Failed to resolve bind address %s:%d: %s", server_ip_address, port, gai_strerror(rc));
		return -1;
	}

	int server_socket = -1;
	for (struct addrinfo *cur = result; cur; cur = cur->ai_next) {
		server_socket = socket(cur->ai_family, cur->ai_socktype, cur->ai_protocol);
		if (server_socket == -1) {
			continue;
		}

		int yes = 1;
		if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) < 0) {
			perror("Failed to set sockopt");
		}

		if (setsockopt(server_socket, SOL_SOCKET, TCP_QUICKACK, &yes, sizeof(int)) < 0) {
			perror("Failed to set sockopt");
		}

		if (setsockopt(server_socket, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(int)) < 0) {
			perror("Failed to set sockopt");
		}

		char host[NI_MAXHOST];
		char port_name[NI_MAXSERV];
		format_sockaddr(cur->ai_addr, cur->ai_addrlen, host, sizeof(host), port_name, sizeof(port_name));
		vcam_log("Binding to %s:%s", host, port_name);

		if (bind(server_socket, cur->ai_addr, cur->ai_addrlen) == -1) {
			perror("Bind failed");
			close(server_socket);
			server_socket = -1;
			continue;
		}

		if (listen(server_socket, 5) == -1) {
			perror("Listening failed");
			close(server_socket);
			server_socket = -1;
			continue;
		}

		vcam_log("Socket listening on %s:%s...", host, port_name);
		break;
	}

	freeaddrinfo(result);
	return server_socket;
}

int ptp_fuji_liveview(int socket);

static void *fuji_accept_remote_ports_thread(void *arg) {
	int event_socket = new_ptp_tcp_socket(FUJI_EVENT_IP_PORT);
	int video_socket = new_ptp_tcp_socket(FUJI_LIVEVIEW_IP_PORT);

	struct sockaddr_storage client_address_event;
	socklen_t client_address_length_event = sizeof(client_address_event);
	int client_socket_event = accept(event_socket, (struct sockaddr *)&client_address_event, &client_address_length_event);
	if (client_socket_event == -1) {
		vcam_log("Failed to accept event socket");
		abort();
	}

	char client_host[NI_MAXHOST];
	char client_port[NI_MAXSERV];
	format_sockaddr((struct sockaddr *)&client_address_event, client_address_length_event, client_host, sizeof(client_host), client_port, sizeof(client_port));
	vcam_log("Event port connection accepted from %s:%s", client_host, client_port);

	struct sockaddr_storage client_address_video;
	socklen_t client_address_length_video = sizeof(client_address_video);
	int client_socket_video = accept(video_socket, (struct sockaddr *)&client_address_video, &client_address_length_video);
	if (client_socket_video == -1) {
		vcam_log("Failed to accept video socket");
		abort();
	}

	format_sockaddr((struct sockaddr *)&client_address_video, client_address_length_video, client_host, sizeof(client_host), client_port, sizeof(client_port));
	vcam_log("Video port connection accepted from %s:%s", client_host, client_port);

	// TODO: Do this continuously with two frames? 
	ptp_fuji_liveview(client_socket_video);

	// TODO: Break loop on sigint?
	while (1) {
		uint32_t temp;
		vcam_log("Liveview/event thread sleeping... (read attempts %d %d)", recv(client_socket_video, &temp, 4, 0), recv(client_socket_event, &temp, 4, 0));
		usleep(1000000);
	}

	return (void *)0;
}

void fuji_accept_remote_ports(void) {
	pthread_t thread;

	if (pthread_create(&thread, NULL, fuji_accept_remote_ports_thread, NULL)) {
		return;
	}

	vcam_log("Started new thread to accept remote ports");
}

int fuji_wifi_main(vcam *cam) {
	struct Fuji *f = fuji(cam);

	vcam_log("Fuji WiFi vcam - running '%s'", cam->model);

	char *this_ip = malloc(64);
	get_local_ip(this_ip);

	// (Skips client datagram discovery)
	if (f->do_tether) {
		vcam_log("Fuji tether connect, skipping datagram");
		fuji_tether_connect("192.168.1.7", 51560);
	}

	// PC AutoSave registration
	if (f->do_register) {
		vcam_log("Fuji register");
		server_ip_address = this_ip;
		fuji_ssdp_register(server_ip_address, "VCAM", "X-H1");
		return 0;
	}

	// PC AutoSave
	if (f->do_discovery) {
		vcam_log("Fuji discovery on %s", this_ip);
		server_ip_address = this_ip;
		fuji_ssdp_import(server_ip_address, "VCAM");
	}

	if (cam->custom_ip_addr) {
		server_ip_address = cam->custom_ip_addr;
		vcam_log("Fuji use local IP: %s", server_ip_address);
	}

	int server_socket = new_ptp_tcp_socket(FUJI_CMD_IP_PORT);
	if (server_socket == -1) {
		vcam_log("Error, make sure to add virtual network device");
		return 1;
	}

	if (cam->sig) {
		vcam_log("Sending signal to parent %d", cam->sig);
		kill(cam->sig, SIGUSR1);
	}

accept_next_client:;
	struct sockaddr_storage client_address;
	socklen_t client_address_length = sizeof(client_address);
	int client_socket = accept(server_socket, (struct sockaddr *)&client_address, &client_address_length);

	if (client_socket == -1) {
		perror("Accept failed");
		close(server_socket);
		return -1;
	}

	reset_client_bridge(cam);
	char client_host[NI_MAXHOST];
	char client_port[NI_MAXSERV];
	format_sockaddr((struct sockaddr *)&client_address, client_address_length, client_host, sizeof(client_host), client_port, sizeof(client_port));
	vcam_log("Connection accepted from %s:%s", client_host, client_port);

	while (1) {
		int rc = tcp_receive_all(cam, client_socket);
		if (rc > 0) {
			break;
		}
		if (rc < 0) {
			vcam_log("Connection forced down");
			break;
		}

		// Now the app has sent the data, and is waiting for a response.

		// Read packet length
		if (tcp_send_all(cam, client_socket)) {
			vcam_log("Connection forced down");
			break;
		}
	}

	close(client_socket);
	vcam_log("Connection closed; waiting for a new client");
	goto accept_next_client;

	close(server_socket);

	return 0;
}
