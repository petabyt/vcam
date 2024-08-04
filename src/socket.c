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
#include <assert.h>

int get_local_ip(char buffer[64]) {
	struct sockaddr_in serv;
	memset(&serv, 0, sizeof(serv));
	serv.sin_family = AF_INET;
	serv.sin_addr.s_addr = inet_addr("1.1.1.1");
	serv.sin_port = htons(1234);
	int sock = socket(AF_INET, SOCK_DGRAM, 0);
	int rc = connect(sock, (const struct sockaddr*) &serv, sizeof(serv));
	if (rc < 0) return rc;

	struct sockaddr_in name;
	socklen_t namelen = sizeof(name);
	rc = getsockname(sock, (struct sockaddr*) &name, &namelen);
	if (rc < 0) return rc;

	const char *p = inet_ntop(AF_INET, &name.sin_addr, buffer, 64);

	return 0;
}

int set_nonblocking_io(int sockfd, int enable) {
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

int tcp_connect_socket(const char *ip, int port) {
	int server_socket;

	printf("Connecting to invite server on %s:%d...\n", ip, port);
	usleep(2 * 1000 * 1000);

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

int tcp_accept_socket(const char *ip, int port) {
	int server_socket;
	struct sockaddr_in server_addr;

	server_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (server_socket < 0) {
		perror("Failed to create TCP socket");
		abort();
	}

	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port);
	server_addr.sin_addr.s_addr = inet_addr(ip);

	int yes = 1;
	if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) < 0) {
		perror("Failed to set sockopt");
		abort();
	}

	if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
		perror("upnp: Binding failed");
		abort();
	}

	if (listen(server_socket, 5) < 0) {
		perror("upnp: Listening failed");
		abort();
	}

	return server_socket;
}
