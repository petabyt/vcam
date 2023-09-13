// Attempt to manually implement upnp
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <ifaddrs.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#if defined(__linux__)
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#endif

struct nl_req_s {
    struct nlmsghdr hdr;
    struct ndmsg gen;
};

int send_query_message(int fd) {
	int seq = rand();
	struct nl_req_s req;
	struct sockaddr_nl sa, dest;
    struct msghdr msg;
    struct iovec iov;

	/* Query the current neighbour table */
	memset (&req, 0, sizeof (req));
	memset (&dest, 0, sizeof (dest));
	memset (&msg, 0, sizeof (msg));

	dest.nl_family = AF_NETLINK;
	req.hdr.nlmsg_len = NLMSG_LENGTH (sizeof (struct ndmsg));
	req.hdr.nlmsg_seq = seq;
	req.hdr.nlmsg_type = RTM_GETNEIGH;
	req.hdr.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;

	req.gen.ndm_family = 2;

	iov.iov_base = &req;
	iov.iov_len = req.hdr.nlmsg_len;

	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_name = &dest;
	msg.msg_namelen = sizeof (dest);

	return sendmsg (fd, (struct msghdr *) &msg, 0);
}

int main() {
	const char *host_ip = "192.168.1.168";
	struct in_addr iface;
	inet_aton(host_ip, &iface);

    // Create a UDP socket
    int udp_socket = socket(PF_NETLINK, SOCK_DGRAM | SOCK_NONBLOCK, NETLINK_ROUTE);
    if (udp_socket < 0) {
        perror("Socket creation error");
        exit(1);
    }
// 
        // /* Enable broadcasting */
        // int boolean = 1;
        // int rc = setsockopt (udp_socket,
                          // SOL_SOCKET,
                          // SO_BROADCAST,
                          // &boolean,
                          // sizeof (boolean));
	// if (rc) {
		// return -1;
	// }
// 
	// unsigned char ttl = 4;
    // rc = setsockopt (udp_socket,
                          // IPPROTO_IP,
                          // IP_MULTICAST_TTL,
                          // &ttl,
                          // sizeof (ttl));

    // Set up the destination address and port
    struct sockaddr_nl dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    //dest_addr.sin_family = AF_INET;
	//dest_addr.sin_port = htons(1900);
	dest_addr.nl_family = AF_NETLINK;

	// memcpy (&(dest_addr.sin_addr),
	                        // &iface,
	                        // sizeof (struct in_addr));

    //inet_pton(AF_INET, host_ip, &(dest_addr.sin_addr));

	if (bind(udp_socket, (struct sockaddr*)&dest_addr, sizeof(dest_addr)) < 0) {
	    perror("bind");
	    exit(1);
	}

	send_query_message(udp_socket);

	struct sockaddr_in servaddr, cliaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    memset(&cliaddr, 0, sizeof(cliaddr));

	puts("Waiting for recv");
	char buffer[8196];
    int rc = recv(udp_socket, (char *)buffer, sizeof(buffer), 0);
	printf("Recieved %d bytes\n", rc);

	FILE *f = fopen("XML_DUMP", "w");
	fwrite(buffer, 1, rc, f);
	fclose(f);

	while (0) {
	    // Define the UPnP alive message
	    const char* upnp_message = "NOTIFY * HTTP/1.1\r\n"
	                               "Host: 239.255.255.250:1900\r\n"
	                               "Cache-Control: max-age=1800\r\n"
	                               "Location: http://192.168.0.120:49152/upnp/CameraDevDesc.xml\r\n"
	                               "NT: urn:schemas-canon-com:service:ICPO-WFTEOSSystemService:1\r\n"
	                               "NTS: ssdp:alive\r\n"
	                               "Server: Camera OS/1.0 UPnP/1.0 Canon Device Discovery/1.0\r\n"
	                               "USN: uuid:00000000-0000-0000-0001-60128B7CC240::urn:schemas-canon-com:service:ICPO-WFTEOSSystemService:1\r\n";

	    // Send the UPnP alive message
	    ssize_t bytes_sent = sendto(udp_socket, upnp_message, strlen(upnp_message), 0, (struct sockaddr*)&dest_addr, sizeof(dest_addr));
	    if (bytes_sent < 0) {
	        perror("Sendto error");
	        close(udp_socket);
	        exit(1);
	    }

	    printf("UPnP Alive message sent successfully.\n");

	    usleep(10000);
	}

    // Close the socket
    close(udp_socket);

    return 0;
}

