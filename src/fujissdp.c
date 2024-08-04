#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include "socket.h"

#define FUJI_AUTOSAVE_REGISTER 51542
#define FUJI_AUTOSAVE_CONNECT 51541
#define FUJI_AUTOSAVE_NOTIFY 51540

struct ClientInfo {
	char ip[64];
	char name[64];
};

static int connect_to_invite_server(int port, const char *ip) {
	printf("Connecting to invite server on %s:%d...\n", ip, port);
	usleep(2 * 1000 * 1000);
	return tcp_connect_socket(ip, port);
}

int fuji_accept_notify(struct ClientInfo *info) {
	int client_socket;
	struct sockaddr_in client_addr;
	socklen_t client_addr_len = sizeof(client_addr);

	int server_socket = tcp_accept_socket("0.0.0.0", FUJI_AUTOSAVE_NOTIFY);
	if (server_socket < 0) {
		printf("Failed to create/accept socket\n");
		abort();
	}

	printf("notify server is listening...\n");

	client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_len);
	if (client_socket < 0) {
		perror("51540: Accepting connection failed");
		abort();
	}

	strncpy(info->ip, inet_ntoa(client_addr.sin_addr), sizeof(info->ip));

	printf("notify server: Connection accepted from %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

	// We expect a NOTIFY message
	char buffer[1024];
	int rc = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
	if (rc < 0) {
		perror("recv fail");
		abort();
	}
	buffer[rc] = '\0';

	printf("Client:\n%s\n", buffer);

	char *saveptr;
	char *delim = " :\r\n";
	char *cur = strtok_r(buffer, delim, &saveptr);
	while (cur != NULL) {
		if (!strcmp(cur, "IMPORTER")) {
			cur = strtok_r(NULL, delim, &saveptr);
			if (cur == NULL) abort();
			printf("Client name: %s\n", cur);
			strcpy(info->name, cur);
		}
		cur = strtok_r(NULL, delim, &saveptr);
	}

	char *http_ok = "HTTP/1.1 200 OK\r\n";
	rc = send(client_socket, http_ok, strlen(http_ok), 0);
	if (rc < 0) abort();

	close(client_socket);
	close(server_socket);

	return 0;
}

int fuji_ssdp_discover(int port, const char *this_ip, char *target_name) {
	int fd;
	struct sockaddr_in broadcast_addr;

	char hello_packet[256];
	sprintf(
		hello_packet,
		"DISCOVER %s HTTP/1.1\r\n"
		"MX:3\r\n"
		"HOST:192.168.1.255:|\r\n"
		"DSCADDR:%s\r\n"
		"SERVICE:PCAUTOSAVE/1.0\r\n",
		target_name, this_ip
	);

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd == -1) {
		perror("Socket creation failed");
		abort();
	}

	int yes = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes)) == -1) {
		perror("setsockopt (SO_BROADCAST)");
		abort();
	}

	memset(&broadcast_addr, 0, sizeof(broadcast_addr));
	broadcast_addr.sin_family = AF_INET;
	broadcast_addr.sin_port = htons(port);
	if (inet_pton(AF_INET, "192.168.1.255", &broadcast_addr.sin_addr) <= 0) {
		perror("Invalid address/ Address not supported");
		abort();
	}

	if (sendto(fd, hello_packet, strlen(hello_packet), 0, (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr)) == -1) {
		perror("Send failed");
		abort();
	}

	return 0;
}

int fuji_ssdp_import(const char *ip, char *name) {
	printf("Sending datagram\n");
	int rc = fuji_ssdp_discover(FUJI_AUTOSAVE_CONNECT, ip, "desktop");
	struct ClientInfo info;
	rc = fuji_accept_notify(&info);

	int fd = connect_to_invite_server(FUJI_AUTOSAVE_CONNECT, info.ip);
	if (fd < 0) {
		printf("Failed to connect to invite server\n");
		abort();
	}

	char register_msg[256];
	sprintf(
		register_msg, 
		"IMPORT * HTTP/1.1\r\n"
		"HOST:%s:%d\r\n"
		"DSCNAME:%s\r\n"
		"DSCPORT:$\r\n",
		info.ip, FUJI_AUTOSAVE_CONNECT, name
	);

	printf("Hello\n");

	rc = send(fd, register_msg, strlen(register_msg), 0);
	if (rc < 0) {
		printf("Failed to send import: %d\n", errno);
		abort();
	}

	char buffer[512];
	rc = recv(fd, buffer, sizeof(buffer) - 1, 0);
	if (rc < 0) {
		perror("recv fail");
		abort();
	} else if (rc == 0) {
		puts("Expected data");
		abort();
	}
	buffer[rc] = '\0';

	printf("Client:\n%s\n", buffer);

	close(fd);

	return 0;
}

int fuji_tether_connect(char *ip, int port) {
	// TODO: Listen to datagrams from client - we don't actually need to though
	connect_to_invite_server(port, ip);
	return 0;
}

int fuji_ssdp_register(const char *ip, char *name, char *model) {
	int rc = fuji_ssdp_discover(FUJI_AUTOSAVE_REGISTER, ip, "*");
	struct ClientInfo info;
	rc = fuji_accept_notify(&info);

	int fd = connect_to_invite_server(FUJI_AUTOSAVE_REGISTER, info.ip);
	if (fd < 0) {
		printf("Failed to connect to invite server\n");
		abort();
	}

	char register_msg[256];
	sprintf(
		register_msg, 
		"REGISTER * HTTP/1.1\r\n"
		"HOST:%s:%d\r\n"
		"DSCNAME:%s\r\n"
		"DSCMODEL:%s\r\n",
		info.ip, FUJI_AUTOSAVE_REGISTER, name, model
	);

	rc = send(fd, register_msg, strlen(register_msg), 0);
	if (rc < 0) abort();

	char buffer[512];
	rc = recv(fd, buffer, sizeof(buffer), 0);
	if (rc < 0) {
		perror("recv fail");
		abort();
	} else if (rc == 0) {
		puts("Expected data");
		abort();
	}
	buffer[rc] = '\0';

	printf("Client:\n%s\n", buffer);

	close(fd);
	return 0;
}

