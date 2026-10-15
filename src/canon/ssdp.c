// Canon EOS camera SSDP/UPnP discovery responder
// Replicates the combined behavior of:
//   - gssdp-vcam (patched libgssdp) resource-group SSDP advertisement
//   - gssdp.c HTTP device-description server
//
// SSDP: responds to ALL M-SEARCH requests (patched want_all=1) with a
// Canon-style 200 OK after a random delay in [0, MX] seconds.
// HTTP: serves CameraDevDesc.xml on port 49152 for any request path.
//
// NOTIFY alive messages carry the gssdp-style Server header (legacy,
// default) or the Canon-style one (--ssdp-modern, modern scheme per miniPtp).
//
// Merged from upnp2/vcam.c. Copyright Daniel C - GNU Lesser General Public License v2.1
#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#include <vcam.h>
#include <canon.h>

/* ------------------------------------------------------------------ */
/* Constants                                                           */
/* ------------------------------------------------------------------ */

#define HTTP_PORT	49152
#define SSDP_ADDR	"239.255.255.250"
#define SSDP_PORT	1900
#define MAX_AGE		1800
#define REANNOUNCE_SEC	599		/* max_age/3 - 1 */
#define MSG_DELAY_MS	120		/* DEFAULT_MESSAGE_DELAY */
#define ANNOUNCE_COUNT	3		/* DEFAULT_ANNOUNCEMENT_SET_SIZE */

#define RESOURCE_TARGET	"urn:schemas-canon-com:service:ICPO-SmartPhoneEOSSystemService:1"
#define RESOURCE_USN	"uuid:00000000-0000-0000-0001-00BBC1859FAB::" RESOURCE_TARGET

#define CANON_SERVER	"Camera OS/1.0 UPnP/1.0 Canon Device Discovery/1.0"

/* Real camera payloads, used unless --local-ip/--ip overrides the host IP */
#define LOCATION_FMT	"http://%s:49152/upnp/CameraDevDesc.xml"
#define URLBASE_FMT	"http://%s:49152/upnp/"

/* ------------------------------------------------------------------ */
/* State (one instance per camera, never a file-scope global)          */
/* ------------------------------------------------------------------ */

#define MAX_TIMERS	256
#define MSGQ_SIZE	128
#define MAX_CLIENTS	16

/* Timer callbacks receive the thread arg so they can find the state */
struct SsdpThreadArg;
typedef void (*timer_fn)(struct SsdpThreadArg *ta, void *arg);

struct timer {
	long long deadline_ms;
	timer_fn fn;
	void *arg;
	int active;
};

struct msgq {
	char *msgs[MSGQ_SIZE];
	int head, tail;
	int pacing;
};

struct SsdpClient {
	int fd;
	int active;
};

struct CanonSsdp {
	int ssdp_fd;
	int tcp_fd;
	char location[128];
	char urlbase[128];
	char server_id[256];
	/* Modern mode (--ssdp-modern): NOTIFY alive carries the Canon-style
	 * Server header that newer cameras / miniPtp-style passive listeners
	 * expect. Default (legacy): gssdp-style Server header. */
	int modern_mode;
	volatile sig_atomic_t running;
	struct timer timers[MAX_TIMERS];
	struct msgq mq;
	struct SsdpClient clients[MAX_CLIENTS];
};

/* Thread argument: the owning vcam, so callbacks can reach the state */
struct SsdpThreadArg {
	vcam *cam;
	struct CanonSsdp *ssdp;
};

/* ------------------------------------------------------------------ */
/* Timer system                                                        */
/* ------------------------------------------------------------------ */

static long long now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void timer_add(struct SsdpThreadArg *ta, long long delay_ms, timer_fn fn, void *arg)
{
	struct CanonSsdp *ssdp = ta->ssdp;

	for (int i = 0; i < MAX_TIMERS; i++) {
		if (!ssdp->timers[i].active) {
			ssdp->timers[i].deadline_ms = now_ms() + delay_ms;
			ssdp->timers[i].fn = fn;
			ssdp->timers[i].arg = arg;
			ssdp->timers[i].active = 1;
			return;
		}
	}
	vcam_log("ssdp: timer_add: no free slots");
}

static void timer_run_expired(struct SsdpThreadArg *ta)
{
	struct CanonSsdp *ssdp = ta->ssdp;
	long long now = now_ms();

	for (int i = 0; i < MAX_TIMERS; i++) {
		if (ssdp->timers[i].active && ssdp->timers[i].deadline_ms <= now) {
			ssdp->timers[i].active = 0;
			ssdp->timers[i].fn(ta, ssdp->timers[i].arg);
		}
	}
}

static int timer_next_timeout(struct SsdpThreadArg *ta)
{
	struct CanonSsdp *ssdp = ta->ssdp;
	long long now = now_ms();
	long long min_wait = -1;

	for (int i = 0; i < MAX_TIMERS; i++) {
		if (ssdp->timers[i].active) {
			long long w = ssdp->timers[i].deadline_ms - now;
			if (w < 0) w = 0;
			if (min_wait < 0 || w < min_wait) min_wait = w;
		}
	}
	return (int)min_wait;
}

/* ------------------------------------------------------------------ */
/* Message queue (120ms pacing between multicast messages)             */
/* ------------------------------------------------------------------ */

static void msgq_tick(struct SsdpThreadArg *ta, void *arg);

static void msgq_push(struct SsdpThreadArg *ta, const char *msg)
{
	struct CanonSsdp *ssdp = ta->ssdp;
	struct msgq *mq = &ssdp->mq;

	if ((mq->tail + 1) % MSGQ_SIZE == mq->head) {
		vcam_log("ssdp: msgq full, dropping");
		return;
	}
	mq->msgs[mq->tail] = strdup(msg);
	mq->tail = (mq->tail + 1) % MSGQ_SIZE;
	if (!mq->pacing) {
		mq->pacing = 1;
		msgq_tick(ta, NULL);
	}
}

static void msgq_tick(struct SsdpThreadArg *ta, void *arg)
{
	(void)arg;
	struct CanonSsdp *ssdp = ta->ssdp;
	struct msgq *mq = &ssdp->mq;

	if (mq->head == mq->tail) {
		mq->pacing = 0;
		return;
	}
	char *msg = mq->msgs[mq->head];
	mq->head = (mq->head + 1) % MSGQ_SIZE;

	struct sockaddr_in dest = {0};
	dest.sin_family = AF_INET;
	dest.sin_port = htons(SSDP_PORT);
	inet_aton(SSDP_ADDR, &dest.sin_addr);
	if (sendto(ssdp->ssdp_fd, msg, strlen(msg), 0,
		   (struct sockaddr *)&dest, sizeof(dest)) < 0)
		vcam_log("ssdp: sendto: %s", strerror(errno));

	free(msg);
	timer_add(ta, MSG_DELAY_MS, msgq_tick, NULL);
}

/* ------------------------------------------------------------------ */
/* SSDP message construction                                           */
/* ------------------------------------------------------------------ */

static void build_alive(struct SsdpThreadArg *ta, char *buf, size_t len)
{
	struct CanonSsdp *ssdp = ta->ssdp;

	snprintf(buf, len,
		"NOTIFY * HTTP/1.1\r\n"
		"Host: %s:%d\r\n"
		"Cache-Control: max-age=%d\r\n"
		"Location: %s\r\n"
		"Server: %s\r\n"
		"NTS: ssdp:alive\r\n"
		"NT: %s\r\n"
		"USN: %s\r\n"
		"\r\n",
		SSDP_ADDR, SSDP_PORT, MAX_AGE,
		ssdp->location,
		ssdp->modern_mode ? CANON_SERVER : ssdp->server_id,
		RESOURCE_TARGET,
		RESOURCE_USN);
}

static void build_byebye(char *buf, size_t len)
{
	snprintf(buf, len,
		"NOTIFY * HTTP/1.1\r\n"
		"Host: %s:%d\r\n"
		"NTS: ssdp:byebye\r\n"
		"NT: %s\r\n"
		"USN: %s\r\n"
		"\r\n",
		SSDP_ADDR, SSDP_PORT,
		RESOURCE_TARGET,
		RESOURCE_USN);
}

static void build_response(struct SsdpThreadArg *ta, char *buf, size_t len)
{
	struct CanonSsdp *ssdp = ta->ssdp;

	snprintf(buf, len,
		"HTTP/1.1 200 OK\r\n"
		"Cache-Control: max-age=%d\r\n"
		"EXT: \r\n"
		"Location: %s\r\n"
		"Server: %s\r\n"
		"ST: %s\r\n"
		"USN: %s\r\n"
		"\r\n",
		MAX_AGE,
		ssdp->location,
		CANON_SERVER,
		RESOURCE_TARGET,
		RESOURCE_USN);
}

static void send_to_group(struct SsdpThreadArg *ta, const char *msg, size_t len)
{
	struct CanonSsdp *ssdp = ta->ssdp;
	struct sockaddr_in dest = {0};

	dest.sin_family = AF_INET;
	dest.sin_port = htons(SSDP_PORT);
	inet_aton(SSDP_ADDR, &dest.sin_addr);
	if (sendto(ssdp->ssdp_fd, msg, len, 0,
		   (struct sockaddr *)&dest, sizeof(dest)) < 0)
		vcam_log("ssdp: sendto: %s", strerror(errno));
}

/* ------------------------------------------------------------------ */
/* Announcement sequences                                              */
/* ------------------------------------------------------------------ */

static void reannounce_tick(struct SsdpThreadArg *ta, void *arg)
{
	(void)arg;
	char msg[1024];

	build_alive(ta, msg, sizeof(msg));
	for (int i = 0; i < ANNOUNCE_COUNT; i++)
		msgq_push(ta, msg);
	timer_add(ta, (long long)REANNOUNCE_SEC * 1000, reannounce_tick, NULL);
}

static void send_initial_announce(struct SsdpThreadArg *ta)
{
	char msg[1024];

	/* initial byebye (unannounce before first announce) */
	build_byebye(msg, sizeof(msg));
	msgq_push(ta, msg);
	/* 3x alive */
	build_alive(ta, msg, sizeof(msg));
	for (int i = 0; i < ANNOUNCE_COUNT; i++)
		msgq_push(ta, msg);
	/* schedule re-announce */
	timer_add(ta, (long long)REANNOUNCE_SEC * 1000, reannounce_tick, NULL);
}

/* ------------------------------------------------------------------ */
/* M-SEARCH handling                                                   */
/* ------------------------------------------------------------------ */

struct delayed_resp {
	struct sockaddr_in dest;
	char msg[512];
};

static void delayed_resp_send(struct SsdpThreadArg *ta, void *arg)
{
	struct CanonSsdp *ssdp = ta->ssdp;
	struct delayed_resp *dr = arg;

	if (sendto(ssdp->ssdp_fd, dr->msg, strlen(dr->msg), 0,
		   (struct sockaddr *)&dr->dest, sizeof(dr->dest)) < 0)
		vcam_log("ssdp: sendto: %s", strerror(errno));
	free(dr);
}

/* Find a header value in an SSDP message (case-insensitive name match).
 * Returns pointer to first char of value (before \r), or NULL. */
static const char *find_header(const char *buf, const char *name)
{
	size_t nlen = strlen(name);
	const char *p = strstr(buf, "\r\n");
	if (!p) return NULL;
	p += 2;
	while (*p && !(p[0] == '\r' && p[1] == '\n')) {
		if (strncasecmp(p, name, nlen) == 0 && p[nlen] == ':') {
			p += nlen + 1;
			while (*p == ' ') p++;
			return p;
		}
		const char *nl = strstr(p, "\r\n");
		if (!nl) return NULL;
		p = nl + 2;
	}
	return NULL;
}

static void header_value(const char *start, char *buf, size_t buflen)
{
	size_t i = 0;
	while (start[i] && start[i] != '\r' && start[i] != '\n' && i < buflen - 1) {
		buf[i] = start[i];
		i++;
	}
	buf[i] = '\0';
}

static void handle_msearch(struct SsdpThreadArg *ta, const char *buf, ssize_t len,
			   struct sockaddr_in *sender)
{
	(void)len;

	/* Validate MAN */
	const char *man = find_header(buf, "MAN");
	if (!man) return;
	char v[256];
	header_value(man, v, sizeof(v));
	if (strcmp(v, "\"ssdp:discover\"") != 0) return;

	/* Validate MX */
	const char *mx = find_header(buf, "MX");
	if (!mx) return;
	header_value(mx, v, sizeof(v));
	int mx_val = atoi(v);
	if (mx_val <= 0) return;

	/* Validate ST */
	const char *st = find_header(buf, "ST");
	if (!st) return;

	/* Respond to ALL M-SEARCH (patched want_all=1) */
	struct delayed_resp *dr = malloc(sizeof(*dr));
	if (!dr) return;
	memcpy(&dr->dest, sender, sizeof(*sender));
	build_response(ta, dr->msg, sizeof(dr->msg));

	long long delay = rand() % ((long long)mx_val * 1000);
	timer_add(ta, delay, delayed_resp_send, dr);
}

static void handle_ssdp_recv(struct SsdpThreadArg *ta)
{
	struct CanonSsdp *ssdp = ta->ssdp;
	char buf[2048];
	struct sockaddr_in sender;
	socklen_t slen = sizeof(sender);
	ssize_t len = recvfrom(ssdp->ssdp_fd, buf, sizeof(buf) - 1, 0,
			       (struct sockaddr *)&sender, &slen);
	if (len < 0) return;
	buf[len] = '\0';

	/* Only handle M-SEARCH (ignore NOTIFY and responses) */
	if (strncmp(buf, "M-SEARCH", 8) == 0)
		handle_msearch(ta, buf, len, &sender);
}

/* ------------------------------------------------------------------ */
/* HTTP device-description server                                      */
/* ------------------------------------------------------------------ */

/* Exact payload from gssdp.c, with the host IP substituted */
static void build_device_xml(struct SsdpThreadArg *ta, char *buf, size_t len)
{
	struct CanonSsdp *ssdp = ta->ssdp;

	snprintf(buf, len,
	    "<?xml version=\"1.0\"?>\n"
	    "<root xmlns=\"urn:schemas-upnp-org:device-1-0\">\n"
	    "  <specVersion>\n"
	    "    <major>1</major>\n"
	    "    <minor>0</minor>\n"
	    "  </specVersion>\n"
	    "  <URLBase>%s</URLBase>\n"
	    "  <device>\n"
	    "    <deviceType>urn:schemas-upnp-org:device:Basic:1</deviceType>\n"
	    "    <friendlyName>Canon EOS Rebel T6</friendlyName>\n"
	    "    <manufacturer>Canon</manufacturer>\n"
	    "    <manufacturerURL>http://www.canon.com/</manufacturerURL>\n"
	    "    <modelDescription>Canon Digital Camera</modelDescription>\n"
	    "    <modelName>Canon EOS vcam</modelName>\n"
	    "    <serialNumber>172073046108</serialNumber>\n"
	    "    <UDN>uuid:00000000-0000-0000-0001-00BBC1859FAB</UDN>\n"
	    "    <serviceList>\n"
	    "      <service>\n"
	    "        <serviceType>urn:schemas-canon-com:service:ICPO-SmartPhoneEOSSystemService:1</serviceType>\n"
	    "        <serviceId>urn:schemas-canon-com:serviceId:ICPO-SmartPhoneEOSSystemService-1</serviceId>\n"
	    "        <SCPDURL>CameraSvcDesc.xml</SCPDURL>\n"
	    "        <controlURL>control/CanonCamera/</controlURL>\n"
	    "        <eventSubURL></eventSubURL>\n"
	    "        <ns:X_targetId xmlns:ns=\"urn:schemas-canon-com:schema-upnp\">uuid:00000000-0000-0000-0001-FFFFFFFFFFFF</ns:X_targetId>\n"
	    "        <ns:X_onService xmlns:ns=\"urn:schemas-canon-com:schema-upnp\">0</ns:X_onService>\n"
	    "        <ns:X_deviceUsbId xmlns:ns=\"urn:schemas-canon-com:schema-upnp\">32b4</ns:X_deviceUsbId>\n"
	    "        <ns:X_deviceNickname xmlns:ns=\"urn:schemas-canon-com:schema-upnp\">Fake Nickname</ns:X_deviceNickname>\n"
	    "      </service>\n"
	    "    </serviceList>\n"
	    "    <presentationURL>/</presentationURL>\n"
	    "  </device>\n"
	    "</root>",
	    ssdp->urlbase);
}

static void handle_tcp_accept(struct SsdpThreadArg *ta)
{
	struct CanonSsdp *ssdp = ta->ssdp;
	struct sockaddr_in addr;
	socklen_t alen = sizeof(addr);
	int cfd = accept(ssdp->tcp_fd, (struct sockaddr *)&addr, &alen);
	if (cfd < 0) return;

	for (int i = 0; i < MAX_CLIENTS; i++) {
		if (!ssdp->clients[i].active) {
			ssdp->clients[i].fd = cfd;
			ssdp->clients[i].active = 1;
			return;
		}
	}
	close(cfd);
}

static void handle_http_client(struct SsdpThreadArg *ta, int cfd)
{
	char req[1024];
	ssize_t n = recv(cfd, req, sizeof(req) - 1, 0);
	if (n < 0) n = 0;
	req[n] = '\0';

	vcam_log("ssdp: http request (%d bytes)", (int)n);

	char device_xml[2048];
	build_device_xml(ta, device_xml, sizeof(device_xml));

	char resp[4096];
	int rlen = snprintf(resp, sizeof(resp),
	    "HTTP/1.1 200 OK\r\n"
	    "Content-Length: %zu\r\n"
	    "Connection: close\r\n"
	    "Content-Type: text/xml\r\n\r\n"
	    "%s",
	    strlen(device_xml), device_xml);

	if (send(cfd, resp, rlen, 0) < 0)
		vcam_log("ssdp: send: %s", strerror(errno));
	vcam_log("ssdp: http response sent");
}

/* ------------------------------------------------------------------ */
/* Interface selection                                                 */
/* ------------------------------------------------------------------ */

static int iface_select(char *name, struct in_addr *ip)
{
	struct ifaddrs *ifa_list, *ifa;

	if (getifaddrs(&ifa_list) != 0) {
		vcam_log("ssdp: getifaddrs: %s", strerror(errno));
		return -1;
	}

	/* First non-loopback UP non-P2P IPv4 interface wins.
	 * Fallback: loopback.  (Mirrors gssdp_net_get_host_ip.) */
	struct ifaddrs *best = NULL;
	struct ifaddrs *fallback = NULL;

	for (ifa = ifa_list; ifa; ifa = ifa->ifa_next) {
		if (!ifa->ifa_addr) continue;
		if (ifa->ifa_addr->sa_family != AF_INET) continue;
		if (!(ifa->ifa_flags & IFF_UP)) continue;
		if (ifa->ifa_flags & IFF_POINTOPOINT) continue;

		if (ifa->ifa_flags & IFF_LOOPBACK) {
			if (!fallback) fallback = ifa;
		} else {
			if (!best) best = ifa;
		}
	}

	struct ifaddrs *chosen = best ? best : fallback;
	if (!chosen) {
		freeifaddrs(ifa_list);
		return -1;
	}

	snprintf(name, IF_NAMESIZE, "%s", chosen->ifa_name);
	*ip = ((struct sockaddr_in *)chosen->ifa_addr)->sin_addr;

	freeifaddrs(ifa_list);
	return 0;
}

/* ------------------------------------------------------------------ */
/* Socket setup                                                        */
/* ------------------------------------------------------------------ */

static int ssdp_setup(const char *ifname, struct in_addr *ip)
{
	int fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		vcam_log("ssdp: socket: %s", strerror(errno));
		return -1;
	}

	int one = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0)
		vcam_log("ssdp: setsockopt(SO_REUSEADDR): %s", strerror(errno));

	struct sockaddr_in addr = {0};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(SSDP_PORT);
	addr.sin_addr.s_addr = INADDR_ANY;

	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		vcam_log("ssdp: bind: %s", strerror(errno));
		close(fd);
		return -1;
	}

	/* Join multicast group on chosen interface */
	struct ip_mreqn mreq = {0};
	inet_aton(SSDP_ADDR, &mreq.imr_multiaddr);
	mreq.imr_address = *ip;
	mreq.imr_ifindex = if_nametoindex(ifname);

	if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
		vcam_log("ssdp: setsockopt(IP_ADD_MEMBERSHIP): %s", strerror(errno));
		close(fd);
		return -1;
	}

	/* Multicast interface */
	if (setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, &mreq, sizeof(mreq)) < 0)
		vcam_log("ssdp: setsockopt(IP_MULTICAST_IF): %s", strerror(errno));

	/* TTL = 4 (UDA 1.0) */
	unsigned char ttl = 4;
	if (setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) < 0)
		vcam_log("ssdp: setsockopt(IP_MULTICAST_TTL): %s", strerror(errno));

	/* Enable loopback so we see our own multicast */
	if (setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, &one, sizeof(one)) < 0)
		vcam_log("ssdp: setsockopt(IP_MULTICAST_LOOP): %s", strerror(errno));

	return fd;
}

static int tcp_setup(void)
{
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		vcam_log("ssdp: socket: %s", strerror(errno));
		return -1;
	}

	int one = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0)
		vcam_log("ssdp: setsockopt(SO_REUSEADDR): %s", strerror(errno));

	struct sockaddr_in addr = {0};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(HTTP_PORT);
	addr.sin_addr.s_addr = INADDR_ANY;

	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		vcam_log("ssdp: bind: %s", strerror(errno));
		close(fd);
		return -1;
	}

	if (listen(fd, 5) < 0) {
		vcam_log("ssdp: listen: %s", strerror(errno));
		close(fd);
		return -1;
	}

	return fd;
}

/* ------------------------------------------------------------------ */
/* Thread main                                                         */
/* ------------------------------------------------------------------ */

static void *canon_ssdp_thread(void *arg)
{
	struct SsdpThreadArg ta = { .cam = ((struct SsdpThreadArg *)arg)->cam };
	struct CanonSsdp *ssdp = ((struct SsdpThreadArg *)arg)->ssdp;
	ta.ssdp = ssdp;

	/* Server ID matching make_server_id(GSSDP_UDA_VERSION_1_0) */
	struct utsname ut;
	if (uname(&ut) == 0)
		snprintf(ssdp->server_id, sizeof(ssdp->server_id), "%s/%s UPnP/1.0 GSSDP/1.6.2",
			 ut.sysname, ut.release);
	else
		snprintf(ssdp->server_id, sizeof(ssdp->server_id), "Linux UPnP/1.0 GSSDP/1.6.2");

	/* Select interface */
	char ifname[IF_NAMESIZE];
	struct in_addr iface_ip;
	if (iface_select(ifname, &iface_ip) < 0) {
		vcam_log("ssdp: no suitable network interface found");
		free(ssdp);
		free(arg);
		return NULL;
	}
	vcam_log("ssdp: interface %s (%s)", ifname, inet_ntoa(iface_ip));

	/* Setup sockets */
	ssdp->ssdp_fd = ssdp_setup(ifname, &iface_ip);
	if (ssdp->ssdp_fd < 0) {
		free(ssdp);
		free(arg);
		return NULL;
	}

	ssdp->tcp_fd = tcp_setup();
	if (ssdp->tcp_fd < 0) {
		close(ssdp->ssdp_fd);
		free(ssdp);
		free(arg);
		return NULL;
	}

	vcam_log("ssdp: %s:%d HTTP :%d mode: %s", SSDP_ADDR, SSDP_PORT, HTTP_PORT,
		 ssdp->modern_mode ? "modern" : "legacy");

	/* Initial announcement: byebye + 3x alive */
	send_initial_announce(&ta);

	/* Main event loop */
	while (ssdp->running) {
		int timeout = timer_next_timeout(&ta);

		struct pollfd fds[2 + MAX_CLIENTS];
		int fd_to_client[2 + MAX_CLIENTS];
		int nfds = 0;

		fds[nfds].fd = ssdp->ssdp_fd;
		fds[nfds].events = POLLIN;
		fd_to_client[nfds] = -1;
		nfds++;

		fds[nfds].fd = ssdp->tcp_fd;
		fds[nfds].events = POLLIN;
		fd_to_client[nfds] = -1;
		nfds++;

		for (int i = 0; i < MAX_CLIENTS; i++) {
			if (ssdp->clients[i].active) {
				fds[nfds].fd = ssdp->clients[i].fd;
				fds[nfds].events = POLLIN;
				fd_to_client[nfds] = i;
				nfds++;
			}
		}

		int rc = poll(fds, nfds, timeout);
		if (rc < 0) {
			if (errno == EINTR) continue;
			vcam_log("ssdp: poll: %s", strerror(errno));
			break;
		}

		/* Run expired timers */
		timer_run_expired(&ta);

		/* Handle network events */
		for (int i = 0; i < nfds; i++) {
			if (!(fds[i].revents & (POLLIN | POLLHUP | POLLERR)))
				continue;

			if (fds[i].fd == ssdp->ssdp_fd) {
				handle_ssdp_recv(&ta);
			} else if (fds[i].fd == ssdp->tcp_fd) {
				handle_tcp_accept(&ta);
			} else {
				int ci = fd_to_client[i];
				if (ci >= 0 && ssdp->clients[ci].active) {
					handle_http_client(&ta, ssdp->clients[ci].fd);
					close(ssdp->clients[ci].fd);
					ssdp->clients[ci].active = 0;
				}
			}
		}
	}

	/* Graceful shutdown: send byebye */
	char msg[1024];
	build_byebye(msg, sizeof(msg));
	send_to_group(&ta, msg, strlen(msg));

	close(ssdp->ssdp_fd);
	close(ssdp->tcp_fd);
	free(arg);
	return NULL;
}

/* ------------------------------------------------------------------ */
/* Public entry point (TCP backend only)                               */
/* ------------------------------------------------------------------ */

int canon_ssdp_start(vcam *cam) {
	/* Hardcoded real-camera payloads, unless --local-ip/--ip was given */
	const char *host_ip = "192.168.1.2";
	if (cam->custom_ip_addr) host_ip = cam->custom_ip_addr;

	struct SsdpThreadArg *arg = malloc(sizeof(*arg));
	if (!arg) return -1;

	arg->cam = cam;
	arg->ssdp = calloc(1, sizeof(struct CanonSsdp));
	if (!arg->ssdp) {
		free(arg);
		return -1;
	}

	arg->ssdp->ssdp_fd = -1;
	arg->ssdp->tcp_fd = -1;
	arg->ssdp->running = 1;
	arg->ssdp->modern_mode = canon(cam)->ssdp_modern;
	snprintf(arg->ssdp->location, sizeof(arg->ssdp->location), LOCATION_FMT, host_ip);
	snprintf(arg->ssdp->urlbase, sizeof(arg->ssdp->urlbase), URLBASE_FMT, host_ip);

	/* Seed for the random M-SEARCH response delay */
	srand((unsigned)(time(NULL) ^ getpid()));

	pthread_t thread;
	if (pthread_create(&thread, NULL, canon_ssdp_thread, arg)) {
		vcam_log("ssdp: failed to create thread");
		free(arg->ssdp);
		free(arg);
		return -1;
	}
	pthread_detach(thread);

	return 0;
}
