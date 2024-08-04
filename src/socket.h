#ifndef SOCKET_H
#define SOCKET_H

int set_nonblocking_io(int sockfd, int enable);
int tcp_connect_socket(const char *ip, int port);
int tcp_accept_socket(const char *ip, int port);

#endif
