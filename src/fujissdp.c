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

// TODO:
// - HTTP buffers not null terminated

#define FUJI_AUTOSAVE_REGISTER 51542
#define FUJI_AUTOSAVE_CONNECT 51541
#define FUJI_AUTOSAVE_NOTIFY 51540

struct ClientInfo {
	char ip[64];
	char name[64];
};

static int set_nonblocking_io(int sockfd, int enable) {
	int flags = fcntl(sockfd, F_GETFL, 0);
	if (flags == -1)
		return -1;

	if (enable) {
		flags |= O_NONBLOCK;
	} else {
		flags &= ~O_NONBLOCK;
	}

	return fcntl(sockfd, F_SETFL, flags);
}

static int connect_to_invite_server(int port, char *ip) {
	int server_socket, client_socket;
	struct sockaddr_in server_addr, client_addr;
	socklen_t client_addr_len = sizeof(client_addr);

	printf("Connecting to invite server on %s:%d...\n", ip, port);
	usleep(1 * 1000 * 1000);

	server_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (server_socket < 0) {
		perror("Failed to create TCP socket");
		abort();
	}

	struct sockaddr_in sa;
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons(port);
	if (inet_pton(AF_INET, ip, &(sa.sin_addr)) <= 0) {
		perror("inet_pton");
		abort();
	}

	set_nonblocking_io(server_socket, 1);

	if (connect(server_socket, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		if (errno != EINPROGRESS) {
			printf("Connect fail\n");
			abort();
		}
	}

	fd_set fdset;
	FD_ZERO(&fdset);
	FD_SET(server_socket, &fdset);
	struct timeval tv;
	tv.tv_sec = 1;
	tv.tv_usec = 0;

	if (select(server_socket + 1, NULL, &fdset, NULL, &tv) == 1) {
		int so_error = 0;
		socklen_t len = sizeof(so_error);
		if (getsockopt(server_socket, SOL_SOCKET, SO_ERROR, &so_error, &len) < 0) {
			printf("Sockopt fail: %d\n", errno);
			abort();
		}

		if (so_error == 0) {
			printf("invite server: Connection established\n");
			set_nonblocking_io(server_socket, 0);
			return server_socket;
		} else {
			printf("so_error: %d\n", so_error);
		}
	} else {
		printf("invite server: Select timed out\n");
	}

	close(server_socket);

	return -1;
}

int fuji_accept_notify(struct ClientInfo *info) {
	int server_socket, client_socket;
	struct sockaddr_in server_addr, client_addr;
	socklen_t client_addr_len = sizeof(client_addr);

	server_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (server_socket < 0) {
		perror("Failed to create TCP socket");
		abort();
	}

	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(51540);
	server_addr.sin_addr.s_addr = INADDR_ANY;

	int yes = 1;
	if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) < 0) {
		perror("Failed to set sockopt");
		abort();
	}

	if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
		perror("upnp: Binding failed");
		abort();
	}

	// Listen for incoming connections
	if (listen(server_socket, 5) < 0) {
		perror("upnp: Listening failed");
		abort();
	}

	printf("51540 server is listening...\n");

	client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_addr_len);
	if (client_socket < 0) {
		perror("51540: Accepting connection failed");
		abort();
	}

	strncpy(info->ip, inet_ntoa(client_addr.sin_addr), sizeof(info->ip));

	printf("51540 server: Connection accepted from %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

	// We expect NOTIFY
	char buffer[1024];
	int rc = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
	if (rc < 0) {
		perror("recv fail");
		abort();
	}
	buffer[rc] = '\0';

	//printf("Client:\n%s\n", buffer);

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

int fuji_ssdp_discover(int port, char *this_ip, char *target_name) {
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

	int broadcastEnable = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable)) == -1) {
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

int fuji_ssdp_import(char *ip, char *name) {
	printf("Sending datagram\n");
	int rc = fuji_ssdp_discover(51541, "192.168.1.39", "desktop");
	struct ClientInfo info;
	rc = fuji_accept_notify(&info);

	usleep(1000 * 1000 * 2);

	int fd = connect_to_invite_server(51541, info.ip);
	if (fd < 0) {
		printf("Failed to connect to invite server\n");
		abort();
	}

	char register_msg[256];
	sprintf(
		register_msg, 
		"IMPORT * HTTP/1.1\r\n"
		"HOST:%s:51541\r\n"
		"DSCNAME:%s\r\n"
		"DSCPORT:$\r\n",
		info.ip, name
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

int fuji_ssdp_register(const char *ip, char *name, char *model) {
	int rc = fuji_ssdp_discover(51542, "192.168.1.39", "*");
	struct ClientInfo info;
	rc = fuji_accept_notify(&info);

	int fd = connect_to_invite_server(51542, info.ip);
	if (fd < 0) {
		printf("Failed to connect to invite server\n");
		abort();
	}

	char register_msg[256];
	sprintf(
		register_msg, 
		"REGISTER * HTTP/1.1\r\n"
		"HOST:%s:51542\r\n"
		"DSCNAME:%s\r\n"
		"DSCMODEL:%s\r\n",
		info.ip, name, model // "FAKE_CAM", "X-H1"
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

	printf("Client:\n%s\n", buffer);

	close(fd);
	return 0;
}

