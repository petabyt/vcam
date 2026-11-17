// WiFi backend: emulates a camera's access point natively.
//
// Lifecycle owner: vcam spawns hostapd and dnsmasq as children with
// PR_SET_PDEATHSIG, so a crash or kill of vcam always takes the daemons
// with it. Teardown (signal/atexit) reverses every change it made:
// children, NetworkManager scoping, the address it added, config files.
//
// Safety rules (never harm the host's main networking):
//  - the interface carrying the default route is never touched
//  - a station-connected interface is never touched
//  - no addr flush, no iptables/NAT, no NM restart (one-iface drop-in only)
//  - regdomain is only *set* when --reg-domain is passed explicitly
//  - dnsmasq binds only the AP interface; the network has no internet,
//    like a real camera
//
// The wifi backend is single-camera per process (like the vhci backend).
//
// Copyright Daniel C - GNU Lesser General Public License v2.1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <net/if.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/genetlink.h>
#include <linux/nl80211.h>
#include <vcam.h>
#include "wifi.h"

#define WIFI_BASE_DIR "/tmp/vcam-wifi"
#define WIFI_STATE_FILE WIFI_BASE_DIR "/state"
#define WIFI_NM_DROPIN "/etc/NetworkManager/conf.d/90-vcam-wifi.conf"

// from linux/if.h - not included here to avoid net/if.h collisions
#define WIFI_OPER_UP 6

// netlink attribute helpers - some distro kernel headers lack them
#ifndef NLA_OK
#define NLA_OK(nla, len) ((len) >= (int)sizeof(struct nlattr) && \
	(nla)->nla_len >= sizeof(struct nlattr) && \
	(nla)->nla_len <= (len))
#define NLA_NEXT(nla, len) ((len) -= NLA_ALIGN((nla)->nla_len), \
	(struct nlattr *)((char *)(nla) + NLA_ALIGN((nla)->nla_len)))
#define NLA_PAYLOAD(nla) ((int)((nla)->nla_len - NLA_HDRLEN))
#define NLA_DATA(nla) ((void *)((char *)(nla) + NLA_HDRLEN))
#endif

// attribute type flags + mask (not in all kernel headers)
#ifndef NLA_F_NESTED
#define NLA_F_NESTED 0x8000
#endif
#ifndef NLA_F_NET_BYTEORDER
#define NLA_F_NET_BYTEORDER 0x4000
#endif
#ifndef NLA_TYPE_MASK
#define NLA_TYPE_MASK ~(NLA_F_NESTED | NLA_F_NET_BYTEORDER)
#endif

// extended ACK attribute (kernel's human-readable error reason)
#ifndef NLMSGERR_ATTR_MSG
#define NLMSGERR_ATTR_MSG 1
#endif

// SSID/password/gateway come from the vendor init functions (cam->ap_*,
// hardcoded per model); this engine adds only the generic defaults.

// Single-camera backend config (like the vhci backend's single device).
// main.c fills it through vcam_wifi_filter_args(vcam_wifi_config(), ...).
static struct vcam_wifi_config wifi_cfg;

struct vcam_wifi_config *vcam_wifi_config(void) {
	return &wifi_cfg;
}

// Runtime state needed by teardown (signal handler / atexit)
struct wifi_state {
	struct vcam_wifi_config *cfg;
	vcam *cam;
	char workdir[128];
	char ssid[64];
	char channel[16];
	char gateway[32];
	char dhcp_start[32];
	char dhcp_end[32];
	// effective identity after the flag > model-init > generic resolution
	char password_eff[64];
	char hostapd_log[160];
	char dnsmasq_log[160];
	char lease_file[160];
	char ctrl_dir[160];
	// What we changed - teardown only reverses recorded changes
	char nm_dropin[128];
	int nm_dropin_created;
	int addr_added;		// we added gateway/24 to the iface
	int iface_was_up;	// iface was already up before we ran
	pid_t hostapd_pid;
	pid_t dnsmasq_pid;
	int hostapd_ctrl;
	/// @brief Set while a station is associated (AP-STA-CONNECTED event,
	/// cleared on AP-STA-DISCONNECTED) - hostapd STATUS has no bare
	/// num_sta key to poll instead
	int sta_connected;
	int teardown_done;
} *wifi_g;

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

static void msleep(int ms) {
	struct timespec ts = { .tv_sec = ms / 1000, .tv_nsec = (long)(ms % 1000) * 1000000 };
	nanosleep(&ts, NULL);
}

static int file_exists(const char *path) {
	struct stat st;
	return stat(path, &st) == 0;
}

// Read a small file, strip trailing newline. Returns bytes read or -1.
static int read_small_file(const char *path, char *buf, int size) {
	int fd = open(path, O_RDONLY);
	if (fd < 0) return -1;
	int n = read(fd, buf, size - 1);
	close(fd);
	if (n < 0) return -1;
	while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == ' ')) n--;
	buf[n] = 0;
	return n;
}

// Last n lines of a log file, for failure diagnostics
static void print_log_tail(const char *path, int lines) {
	if (!file_exists(path)) return;
	FILE *f = fopen(path, "r");
	if (!f) return;
	char ring[16][200];
	memset(ring, 0, sizeof(ring));
	int pos = 0, total = 0;
	char line[200];
	while (fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\n")] = 0;
		snprintf(ring[pos], sizeof(ring[0]), "%s", line);
		pos = (pos + 1) % 16;
		total++;
	}
	fclose(f);
	int start = total > lines ? total - lines : 0;
	int idx = (pos + 16 - (total - start)) % 16;
	for (int i = start; i < total; i++) {
		vcam_log("    | %s", ring[idx]);
		idx = (idx + 1) % 16;
	}
}

// ---------------------------------------------------------------------------
// Netlink core
// ---------------------------------------------------------------------------

#define NL_BUF_SIZE 65536

struct nlsock {
	int fd;
	uint32_t seq;
	// recv scratch: one recvmsg may coalesce many netlink messages, so
	// the batch is buffered and parsed one message at a time
	unsigned char *buf;
	int buflen;
	int offset;
};

static int nl_open(struct nlsock *n, int protocol) {
	n->fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, protocol);
	if (n->fd < 0) return -1;
	n->seq = 1;

	n->buf = malloc(NL_BUF_SIZE);
	if (n->buf == NULL) {
		close(n->fd);
		return -1;
	}
	n->buflen = 0;
	n->offset = 0;

	struct sockaddr_nl nladdr = {0};
	nladdr.nl_family = AF_NETLINK;
	if (bind(n->fd, (struct sockaddr *)&nladdr, sizeof(nladdr)) < 0) {
		free(n->buf);
		close(n->fd);
		return -1;
	}
	return 0;
}

static void nl_close(struct nlsock *n) {
	close(n->fd);
	free(n->buf);
	n->buf = NULL;
}

static int nl_send(struct nlsock *n, uint16_t type, uint16_t flags, void *payload, size_t plen) {
	struct {
		struct nlmsghdr nlh;
		unsigned char buf[256];
	} msg = {0};
	if (plen > sizeof(msg.buf)) return -1;

	// any unparsed batch bytes belong to the previous request - stale now
	n->buflen = 0;
	n->offset = 0;

	struct sockaddr_nl nladdr = {0};
	nladdr.nl_family = AF_NETLINK;

	static char empty;
	struct iovec iov[2] = {
		{ &msg.nlh, sizeof(msg.nlh) },
		{ plen ? payload : (void *)&empty, plen },
	};

	msg.nlh.nlmsg_len = NLMSG_LENGTH(plen);
	msg.nlh.nlmsg_type = type;
	msg.nlh.nlmsg_flags = NLM_F_REQUEST | flags;
	msg.nlh.nlmsg_seq = ++n->seq;
	msg.nlh.nlmsg_pid = 0;
	if (plen) memcpy(msg.buf, payload, plen);

	struct msghdr mh = {0};
	mh.msg_name = &nladdr;
	mh.msg_namelen = sizeof(nladdr);
	mh.msg_iov = iov;
	mh.msg_iovlen = plen ? 2 : 1;

	if (sendmsg(n->fd, &mh, 0) < 0) return -1;
	return (int)msg.nlh.nlmsg_seq;
}

// Extract the kernel's extended-ACK reason from an error reply (best
// effort - attrs live after the echoed request header)
static void nl_log_extack(struct nlmsghdr *nlh) {
	unsigned char *base = (unsigned char *)nlh;
	int off = NLMSG_ALIGN(sizeof(struct nlmsgerr));
	int remaining = (int)nlh->nlmsg_len - off;
	if (remaining <= 0) return;

	while (remaining >= (int)sizeof(struct nlattr)) {
		struct nlattr *attr = (struct nlattr *)(base + off);
		if (attr->nla_len < sizeof(struct nlattr) || attr->nla_len > remaining) break;
		if ((attr->nla_type & NLA_TYPE_MASK) == NLMSGERR_ATTR_MSG) {
			vcam_log("wifi: kernel says: %.*s", NLA_PAYLOAD(attr) > 120 ? 120 : NLA_PAYLOAD(attr), (char *)NLA_DATA(attr));
			return;
		}
		off += NLA_ALIGN(attr->nla_len);
		remaining -= NLA_ALIGN(attr->nla_len);
	}
}

// Receive the next netlink reply message. One recvmsg may coalesce many
// messages (the kernel batches dumps), so messages are parsed from the
// socket's scratch buffer and only refilled when exhausted.
// Returns the message length, 0 on NLMSG_DONE or a 0-error ACK (NLM_F_ACK),
// -1 on error (errno is set from the error message).
static ssize_t nl_recv_one(struct nlsock *n, struct nlmsghdr **out) {
	while (1) {
		if (n->offset >= n->buflen) {
			struct iovec iov = { n->buf, NL_BUF_SIZE };
			struct sockaddr_nl nladdr = {0};
			struct msghdr mh = {0};
			mh.msg_name = &nladdr;
			mh.msg_namelen = sizeof(nladdr);
			mh.msg_iov = &iov;
			mh.msg_iovlen = 1;

			ssize_t rc = recvmsg(n->fd, &mh, 0);
			if (rc < 0) return -1;
			if (mh.msg_flags & MSG_TRUNC) {
				// batch larger than the scratch buffer - would
				// silently lose interfaces
				errno = ENOSPC;
				return -1;
			}
			n->buflen = (int)rc;
			n->offset = 0;
			if (rc == 0) return -1; // EOF on the netlink socket
			continue;
		}

		struct nlmsghdr *nlh = (struct nlmsghdr *)(n->buf + n->offset);
		if (!NLMSG_OK(nlh, (unsigned)(n->buflen - n->offset))) {
			errno = EIO;
			return -1;
		}
		n->offset += NLMSG_ALIGN(nlh->nlmsg_len);

		// skip stale replies from an earlier request individually -
		// a batch may span two requests
		if (nlh->nlmsg_seq != n->seq) continue;
		if (nlh->nlmsg_type == NLMSG_DONE) return 0;
		if (nlh->nlmsg_type == NLMSG_ERROR) {
			struct nlmsgerr *err = NLMSG_DATA(nlh);
			if (err->error != 0) nl_log_extack(nlh);
			errno = -err->error;
			return err->error == 0 ? 0 : -1;
		}
		*out = nlh;
		return nlh->nlmsg_len;
	}
}

// ---------------------------------------------------------------------------
// rtnetlink: links, addresses, routes
// ---------------------------------------------------------------------------

struct link_info {
	char name[IFNAMSIZ];
	int index;
	unsigned flags;
	int operstate;	// IFLA_OPERSTATE
	int carrier;	// IFLA_CARRIER, -1 when unknown
};

// Callback returns nonzero to stop the dump early
typedef int (*nl_dump_cb)(struct nlmsghdr *nlh, void *arg);

// Send a dump request. The kernel validates dump requests against the
// per-type header size (ifinfomsg for links, rtmsg for routes, ifaddrmsg
// for addresses), so callers build their own - a short rtgenmsg would make
// the whole dump fail with EINVAL and look like "no interfaces".
static int nl_dump_msg(struct nlsock *n, uint16_t type, void *hdr, size_t len) {
	return nl_send(n, type, NLM_F_DUMP, hdr, len);
}

// Receive loop for a dump previously sent with nl_dump_msg(). The callback
// returning nonzero stops the iteration early.
static void nl_recv_loop(struct nlsock *n, nl_dump_cb cb, void *arg) {
	while (1) {
		struct nlmsghdr *nlh = NULL;
		ssize_t rc = nl_recv_one(n, &nlh);
		if (rc <= 0) return;
		if (cb(nlh, arg)) return;
	}
}

// Fill info from RTM_NEWLINK. Returns 1 when a link was processed.
static int link_cb(struct nlmsghdr *nlh, void *arg) {
	if (nlh->nlmsg_type != RTM_NEWLINK) return 0;
	struct link_info *info = arg;

	struct ifinfomsg *ifi = NLMSG_DATA(nlh);
	struct rtattr *rta = IFLA_RTA(ifi);
	int remaining = RTM_PAYLOAD(nlh);

	memset(info, 0, sizeof(*info));
	info->index = ifi->ifi_index;
	info->carrier = -1;

	while (RTA_OK(rta, remaining)) {
		uint8_t u8;
		switch (rta->rta_type) {
		case IFLA_IFNAME:
			snprintf(info->name, sizeof(info->name), "%s", (char *)RTA_DATA(rta));
			break;
		case IFLA_OPERSTATE:
			memcpy(&u8, RTA_DATA(rta), sizeof(u8));
			info->operstate = u8;
			break;
		case IFLA_CARRIER:
			memcpy(&u8, RTA_DATA(rta), sizeof(u8));
			info->carrier = u8;
			break;
		}
		rta = RTA_NEXT(rta, remaining);
	}
	info->flags = ifi->ifi_flags;
	return info->name[0] != 0;
}

// Get one interface's info by name. Returns 0 on hit, 1 on miss.
static int rt_get_link(struct nlsock *n, const char *ifname, struct link_info *out) {
	struct ifinfomsg gen = {0};
	gen.ifi_family = AF_UNSPEC;
	nl_dump_msg(n, RTM_GETLINK, &gen, sizeof(gen));

	while (1) {
		struct nlmsghdr *nlh = NULL;
		ssize_t rc = nl_recv_one(n, &nlh);
		if (rc <= 0) return 1;
		if (link_cb(nlh, out) && !strcmp(out->name, ifname)) return 0;
	}
}

static int default_route_cb(struct nlmsghdr *nlh, void *arg) {
	if (nlh->nlmsg_type != RTM_NEWROUTE) return 0;
	int *oif = arg;

	struct rtmsg *rt = NLMSG_DATA(nlh);
	if (rt->rtm_dst_len != 0) return 0; // not a default route

	struct rtattr *rta = RTM_RTA(rt);
	int remaining = RTM_PAYLOAD(nlh);
	while (RTA_OK(rta, remaining)) {
		if (rta->rta_type == RTA_OIF) {
			memcpy(oif, RTA_DATA(rta), sizeof(int));
			return 1; // stop at first default route with an oif
		}
		rta = RTA_NEXT(rta, remaining);
	}
	return 0;
}

// Interface index that carries the default route (0 when none found)
static int rt_get_default_route_if(struct nlsock *n) {
	int oif = 0;
	struct rtmsg gen = {0};
	gen.rtm_family = AF_UNSPEC;
	nl_dump_msg(n, RTM_GETROUTE, &gen, sizeof(gen));
	nl_recv_loop(n, default_route_cb, &oif);
	return oif;
}

struct addr_entry {
	char ifname[IFNAMSIZ];
	char ip[INET_ADDRSTRLEN];
	uint8_t prefixlen;
};

// Fill entry from RTM_NEWADDR. Returns 1 when an IPv4 address was found.
static int addr_cb(struct nlmsghdr *nlh, void *arg) {
	if (nlh->nlmsg_type != RTM_NEWADDR) return 0;
	struct addr_entry *out = arg;

	struct ifaddrmsg *ifa = NLMSG_DATA(nlh);
	if (ifa->ifa_family != AF_INET) return 0;

	struct rtattr *rta = IFA_RTA(ifa);
	int remaining = IFA_PAYLOAD(nlh);
	memset(out, 0, sizeof(*out));
	out->prefixlen = ifa->ifa_prefixlen;

	while (RTA_OK(rta, remaining)) {
		if (rta->rta_type == IFA_LOCAL) {
			inet_ntop(AF_INET, RTA_DATA(rta), out->ip, sizeof(out->ip));
		} else if (rta->rta_type == IFA_LABEL) {
			snprintf(out->ifname, sizeof(out->ifname), "%s", (char *)RTA_DATA(rta));
		}
		rta = RTA_NEXT(rta, remaining);
	}
	// IFA_LABEL may be absent on hidden devices - keep the address but
	// mark the iface unknown (name starts with 0)
	return out->ip[0] != 0;
}

typedef void (*addr_iter_cb)(struct addr_entry *addr, void *arg);

static void rt_iter_addrs(struct nlsock *n, addr_iter_cb cb, void *arg) {
	struct ifaddrmsg gen = {0};
	gen.ifa_family = AF_INET;
	nl_dump_msg(n, RTM_GETADDR, &gen, sizeof(gen));

	struct addr_entry entry;
	while (1) {
		struct nlmsghdr *nlh = NULL;
		ssize_t rc = nl_recv_one(n, &nlh);
		if (rc <= 0) return;
		if (addr_cb(nlh, &entry)) cb(&entry, arg);
	}
}

// Set/clear IFF_UP on an interface. Returns 0 or -1 (errno set).
static int rt_set_link_up(struct nlsock *n, int ifindex, int up) {
	struct ifinfomsg ifi = {0};
	ifi.ifi_family = AF_UNSPEC;
	ifi.ifi_index = ifindex;
	ifi.ifi_flags = up ? IFF_UP : 0;
	ifi.ifi_change = IFF_UP;

	if (nl_send(n, RTM_NEWLINK, NLM_F_ACK, &ifi, sizeof(ifi)) < 0) return -1;
	struct nlmsghdr *nlh = NULL;
	return nl_recv_one(n, &nlh) == 0 ? 0 : -1;
}

// Build an RTM_NEWADDR/RTM_DELADDR payload: ifaddrmsg + IFA_LOCAL attr.
// (nl_send adds the netlink header - a pre-built one would double-wrap the
// message and the kernel would parse the inner header as ifaddrmsg)
static ssize_t build_ifa_payload(int ifindex, const char *ip, int prefixlen,
				 void *buf, size_t bufsize) {
	struct in_addr addr;
	if (inet_pton(AF_INET, ip, &addr) != 1) return -1;

	struct {
		struct ifaddrmsg ifa;
		char attr[RTA_LENGTH(4)];
	} *msg = buf;

	if (sizeof(*msg) > bufsize) return -1;
	memset(buf, 0, sizeof(*msg));

	msg->ifa.ifa_family = AF_INET;
	msg->ifa.ifa_prefixlen = prefixlen;
	msg->ifa.ifa_index = ifindex;
	msg->ifa.ifa_scope = RT_SCOPE_UNIVERSE;

	struct rtattr *rta = (struct rtattr *)msg->attr;
	rta->rta_type = IFA_LOCAL;
	rta->rta_len = RTA_LENGTH(4);
	memcpy(RTA_DATA(rta), &addr, 4);

	return sizeof(*msg);
}

static int rt_change_addr(struct nlsock *n, uint16_t type, int ifindex, const char *ip, int prefixlen) {
	unsigned char buf[64];
	ssize_t len = build_ifa_payload(ifindex, ip, prefixlen, buf, sizeof(buf));
	if (len < 0) return -EINVAL;

	if (nl_send(n, type, NLM_F_ACK, buf, (size_t)len) < 0) return -errno;
	struct nlmsghdr *nlh = NULL;
	ssize_t rc = nl_recv_one(n, &nlh);
	if (rc < 0) return -errno;
	return 0;
}

static int rt_add_addr(struct nlsock *n, int ifindex, const char *ip, int prefixlen) {
	return rt_change_addr(n, RTM_NEWADDR, ifindex, ip, prefixlen);
}

static int rt_del_addr(struct nlsock *n, int ifindex, const char *ip, int prefixlen) {
	return rt_change_addr(n, RTM_DELADDR, ifindex, ip, prefixlen);
}

// ---------------------------------------------------------------------------
// nl80211: wifi capability enumeration, regdomain
// ---------------------------------------------------------------------------

// Resolve a generic netlink family id by name
static int genl_resolve(struct nlsock *n, const char *family) {
	struct genlmsghdr genl = {0};
	genl.cmd = CTRL_CMD_GETFAMILY;

	unsigned char buf[128];
	memset(buf, 0, sizeof(buf));
	memcpy(buf, &genl, sizeof(genl));

	struct nlattr attr = {0};
	attr.nla_type = CTRL_ATTR_FAMILY_NAME;
	attr.nla_len = NLA_HDRLEN + strlen(family) + 1;
	memcpy(buf + NLMSG_ALIGN(sizeof(genl)), &attr, sizeof(attr));
	memcpy(buf + NLMSG_ALIGN(sizeof(genl)) + sizeof(attr), family, strlen(family) + 1);
	size_t plen = NLMSG_ALIGN(sizeof(genl)) + NLMSG_ALIGN(attr.nla_len);

	nl_send(n, GENL_ID_CTRL, 0, buf, plen);

	struct nlmsghdr *nlh = NULL;
	ssize_t rc = nl_recv_one(n, &nlh);
	if (rc <= 0) return -1;

	struct genlmsghdr *ghdr = NLMSG_DATA(nlh);
	struct nlattr *rta = (struct nlattr *)((char *)ghdr + NLMSG_ALIGN(sizeof(*ghdr)));
	int remaining = NLMSG_PAYLOAD(nlh, sizeof(*ghdr));
	while (NLA_OK(rta, remaining)) {
		if (rta->nla_type == CTRL_ATTR_FAMILY_ID) {
			uint16_t id;
			memcpy(&id, NLA_DATA(rta), sizeof(id));
			return id;
		}
		rta = NLA_NEXT(rta, remaining);
	}
	return -1;
}

// Send an nl80211 command and invoke cb on the reply. Returns cb result
// (0/1) or -1 on transport error.
static int nl80211_cmd(struct nlsock *n, int family, uint8_t cmd, uint16_t flags,
		       void *payload, size_t plen, nl_dump_cb cb, void *arg) {
	struct {
		struct nlmsghdr nlh;
		unsigned char buf[300];
	} msg = {0};

	struct genlmsghdr genl = {0};
	genl.cmd = cmd;
	memcpy(msg.buf, &genl, sizeof(genl));
	size_t total = NLMSG_ALIGN(sizeof(genl));
	if (payload && plen) {
		if (NLMSG_ALIGN(plen) > sizeof(msg.buf) - total) return -1;
		memcpy(msg.buf + total, payload, plen);
		total += NLMSG_ALIGN(plen);
	}

	msg.nlh.nlmsg_len = NLMSG_LENGTH(total);
	msg.nlh.nlmsg_type = family;
	msg.nlh.nlmsg_flags = NLM_F_REQUEST | flags;
	msg.nlh.nlmsg_seq = ++n->seq;

	struct sockaddr_nl nladdr = {0};
	nladdr.nl_family = AF_NETLINK;
	struct iovec iov = { &msg, msg.nlh.nlmsg_len };
	struct msghdr mh = {0};
	mh.msg_name = &nladdr;
	mh.msg_namelen = sizeof(nladdr);
	mh.msg_iov = &iov;
	mh.msg_iovlen = 1;
	if (sendmsg(n->fd, &mh, 0) < 0) return -1;

	while (1) {
		struct nlmsghdr *nlh = NULL;
		ssize_t rc = nl_recv_one(n, &nlh);
		if (rc <= 0) return (int)rc;
		int cbrc = cb ? cb(nlh, arg) : 0;
		if (cbrc || !(nlh->nlmsg_flags & NLM_F_MULTI)) return cbrc;
	}
}

// small helper: one u32 attribute in a buffer, returns its aligned size
static size_t put_attr_u32(void *buf, uint16_t type, uint32_t value) {
	struct nlattr *attr = buf;
	attr->nla_type = type;
	attr->nla_len = NLA_HDRLEN + 4;
	memcpy(NLA_DATA(attr), &value, 4);
	return NLA_ALIGN(attr->nla_len);
}

// GET_INTERFACE/GET_WIPHY reply: extract NL80211_ATTR_WIPHY
static int attr_wiphy_cb(struct nlmsghdr *nlh, void *arg) {
	int *wiphy = arg;
	struct genlmsghdr *ghdr = NLMSG_DATA(nlh);
	if (ghdr->cmd != NL80211_CMD_NEW_INTERFACE && ghdr->cmd != NL80211_CMD_NEW_WIPHY) return 0;

	struct nlattr *rta = (struct nlattr *)((char *)ghdr + NLMSG_ALIGN(sizeof(*ghdr)));
	int remaining = NLMSG_PAYLOAD(nlh, sizeof(*ghdr));
	while (NLA_OK(rta, remaining)) {
		if (rta->nla_type == NL80211_ATTR_WIPHY) {
			memcpy(wiphy, NLA_DATA(rta), sizeof(int));
			return 1;
		}
		rta = NLA_NEXT(rta, remaining);
	}
	return 0;
}

// Find the wiphy (phy) index an interface belongs to (-1 when unknown)
static int nl80211_get_wiphy_of(struct nlsock *n, int family, int ifindex) {
	unsigned char payload[16];
	size_t plen = put_attr_u32(payload, NL80211_ATTR_IFINDEX, (uint32_t)ifindex);

	int wiphy = -1;
	nl80211_cmd(n, family, NL80211_CMD_GET_INTERFACE, 0, payload, plen, attr_wiphy_cb, &wiphy);
	return wiphy;
}

// Does the wiphy support AP mode? (NL80211_ATTR_SUPPORTED_IFTYPES nest)
//
// The outer attribute arrives with NLA_F_NESTED set in nla_type, and the
// sub-attributes are NLA_FLAG entries whose nla_type carries the iftype
// number (nla_put_flag, no payload) - so AP support is "a sub-attr typed
// NL80211_IFTYPE_AP exists", not a value inside it.
static int wiphy_ap_cb(struct nlmsghdr *nlh, void *arg) {
	int *ap_ok = arg;
	struct genlmsghdr *ghdr = NLMSG_DATA(nlh);
	if (ghdr->cmd != NL80211_CMD_NEW_WIPHY) return 0;

	struct nlattr *rta = (struct nlattr *)((char *)ghdr + NLMSG_ALIGN(sizeof(*ghdr)));
	int remaining = NLMSG_PAYLOAD(nlh, sizeof(*ghdr));
	while (NLA_OK(rta, remaining)) {
		if ((rta->nla_type & NLA_TYPE_MASK) == NL80211_ATTR_SUPPORTED_IFTYPES) {
			struct nlattr *sub = NLA_DATA(rta);
			int sublen = NLA_PAYLOAD(rta);
			while (NLA_OK(sub, sublen)) {
				if ((sub->nla_type & NLA_TYPE_MASK) == NL80211_IFTYPE_AP)
					*ap_ok = 1;
				sub = NLA_NEXT(sub, sublen);
			}
		}
		rta = NLA_NEXT(rta, remaining);
	}
	return 1;
}

static int nl80211_wiphy_ap_capable(struct nlsock *n, int family, int wiphy_idx) {
	unsigned char payload[16];
	size_t plen = put_attr_u32(payload, NL80211_ATTR_WIPHY, (uint32_t)wiphy_idx);

	int ap_ok = 0;
	nl80211_cmd(n, family, NL80211_CMD_GET_WIPHY, 0, payload, plen, wiphy_ap_cb, &ap_ok);
	return ap_ok;
}

// Read the regulatory domain alpha2 into out ("00" when unset)
static int regdomain_cb(struct nlmsghdr *nlh, void *arg) {
	char *out = arg;
	struct genlmsghdr *ghdr = NLMSG_DATA(nlh);
	if (ghdr->cmd != NL80211_CMD_GET_REG) return 0;
	struct nlattr *rta = (struct nlattr *)((char *)ghdr + NLMSG_ALIGN(sizeof(*ghdr)));
	int remaining = NLMSG_PAYLOAD(nlh, sizeof(*ghdr));
	while (NLA_OK(rta, remaining)) {
		if (rta->nla_type == NL80211_ATTR_REG_ALPHA2) {
			// alpha2 is not always NUL terminated in the attribute
			memcpy(out, NLA_DATA(rta), 2);
			out[2] = 0;
			return 1;
		}
		rta = NLA_NEXT(rta, remaining);
	}
	return 0;
}

static int nl80211_get_regdomain(struct nlsock *n, int family, char out[4]) {
	out[0] = 0;
	nl80211_cmd(n, family, NL80211_CMD_GET_REG, 0, NULL, 0, regdomain_cb, out);
	return out[0] ? 0 : -1;
}

// Setting the regdomain is global to the kernel: only done on explicit
// request (--reg-domain), never implicitly
static int nl80211_set_regdomain(struct nlsock *n, int family, const char *alpha2) {
	unsigned char payload[16];
	struct nlattr *attr = (struct nlattr *)payload;
	attr->nla_type = NL80211_ATTR_REG_ALPHA2;
	attr->nla_len = NLA_HDRLEN + 3;
	memcpy(NLA_DATA(attr), alpha2, 3);

	return nl80211_cmd(n, family, NL80211_CMD_SET_REG, NLM_F_ACK, payload,
			   NLA_ALIGN(attr->nla_len), NULL, NULL);
}

// ---------------------------------------------------------------------------
// Interface discovery
// ---------------------------------------------------------------------------

struct wifi_iface {
	char name[IFNAMSIZ];
	char driver[64];
	int index;
	int ap_capable;
	int is_default_route;
	int station_connected;	// up + operstate UP + carrier
};

struct wifi_ifaces {
	struct wifi_iface ifs[16];
	int count;
	int default_route_if;
	// every link seen in the dump + why it was skipped (for --check)
	struct wifi_link_seen {
		char name[IFNAMSIZ];
		char driver[64];
		int wifi;	// has phy80211
		int skipped;
		char reason[64];
	} seen[32];
	int seen_count;
};

static int is_wifi_iface(const char *ifname) {
	char path[128];
	snprintf(path, sizeof(path), "/sys/class/net/%s/phy80211", ifname);
	return file_exists(path);
}

static void read_driver_name(const char *ifname, char *out, size_t size) {
	char path[160];
	snprintf(path, sizeof(path), "/sys/class/net/%s/device/driver", ifname);
	char link[256];
	ssize_t n = readlink(path, link, sizeof(link) - 1);
	if (n < 0) {
		snprintf(out, size, "unknown");
		return;
	}
	link[n] = 0;
	char *base = strrchr(link, '/');
	snprintf(out, size, "%s", base ? base + 1 : link);
}

static void collect_iface(struct link_info *li, struct wifi_ifaces *out) {
	if (li->name[0] == 0 || out->seen_count >= 32) return;

	struct wifi_link_seen *s = &out->seen[out->seen_count++];
	memset(s, 0, sizeof(*s));
	snprintf(s->name, sizeof(s->name), "%s", li->name);
	s->wifi = is_wifi_iface(li->name);

	if (out->count >= 16) {
		snprintf(s->reason, sizeof(s->reason), "too many interfaces");
		return;
	}
	if (!s->wifi) {
		snprintf(s->reason, sizeof(s->reason), "not a wifi interface");
		return;
	}
	read_driver_name(li->name, s->driver, sizeof(s->driver));

	struct wifi_iface *w = &out->ifs[out->count++];
	memset(w, 0, sizeof(*w));
	snprintf(w->name, sizeof(w->name), "%s", li->name);
	w->index = li->index;
	snprintf(w->driver, sizeof(w->driver), "%s", s->driver);
	w->is_default_route = (out->default_route_if == li->index);
	w->station_connected = (li->flags & IFF_UP) && li->carrier == 1 &&
			       li->operstate == WIFI_OPER_UP;
}

// n = NETLINK_ROUTE socket for link dumps, gn = NETLINK_GENERIC for nl80211
static int enum_wifi_ifaces(struct nlsock *n, struct nlsock *gn, int family,
			    struct wifi_ifaces *out) {
	memset(out, 0, sizeof(*out));
	out->default_route_if = rt_get_default_route_if(n);

	struct ifinfomsg gen = {0};
	gen.ifi_family = AF_UNSPEC;
	nl_dump_msg(n, RTM_GETLINK, &gen, sizeof(gen));

	struct link_info li;
	while (1) {
		struct nlmsghdr *nlh = NULL;
		ssize_t rc = nl_recv_one(n, &nlh);
		if (rc <= 0) break;
		if (link_cb(nlh, &li)) collect_iface(&li, out);
	}

	for (int i = 0; i < out->count; i++) {
		int wiphy = nl80211_get_wiphy_of(gn, family, out->ifs[i].index);
		if (wiphy < 0) {
			vcam_log("wifi: could not resolve wiphy for %s (nl80211 query failed)", out->ifs[i].name);
			continue;
		}
		out->ifs[i].ap_capable = nl80211_wiphy_ap_capable(gn, family, wiphy);
	}
	return 0;
}

// ---------------------------------------------------------------------------
// Interface selection - the "do not screw up the main wifi" rules
// ---------------------------------------------------------------------------

// Pick the AP interface. Never selects the default-route interface or a
// station-connected interface; ath* drivers are preferred (Atheros cards
// have the most complete AP support; iwlwifi AP mode is restricted).
static const struct wifi_iface *pick_ap_iface(struct wifi_ifaces *ifs, const char *want) {
	if (ifs->count == 0) {
		vcam_log("wifi: no wifi interfaces found");
		return NULL;
	}

	if (want != NULL && want[0]) {
		for (int i = 0; i < ifs->count; i++) {
			if (strcmp(ifs->ifs[i].name, want)) continue;
			if (ifs->ifs[i].is_default_route)
				vcam_log("wifi: WARNING %s carries the default route - proceeding on explicit request", want);
			if (ifs->ifs[i].station_connected)
				vcam_log("wifi: WARNING %s has an active station connection - proceeding on explicit request", want);
			if (!ifs->ifs[i].ap_capable)
				vcam_log("wifi: WARNING %s does not advertise AP mode", want);
			return &ifs->ifs[i];
		}
		vcam_log("wifi: interface %s not found", want);
		return NULL;
	}

	const struct wifi_iface *best = NULL;
	for (int i = 0; i < ifs->count; i++) {
		const struct wifi_iface *w = &ifs->ifs[i];
		if (w->is_default_route) continue;
		if (w->station_connected) continue;
		if (!w->ap_capable) continue;
		if (best == NULL) {
			best = w;
			continue;
		}
		// ath* beats everything else (iwlwifi demoted)
		if (!strncmp(w->driver, "ath", 3) && strncmp(best->driver, "ath", 3))
			best = w;
	}

	if (best == NULL) {
		vcam_log("wifi: no AP-capable interface found that is not your default route or a connected station:");
		for (int i = 0; i < ifs->count; i++) {
			vcam_log("wifi:   %s driver=%s ap=%s default_route=%s connected=%s",
				ifs->ifs[i].name, ifs->ifs[i].driver,
				ifs->ifs[i].ap_capable ? "yes" : "no",
				ifs->ifs[i].is_default_route ? "yes" : "no",
				ifs->ifs[i].station_connected ? "yes" : "no");
		}
		vcam_log("wifi: pass --wifi-dev <if> to override");
		return NULL;
	}
	return best;
}

// ---------------------------------------------------------------------------
// NetworkManager scoping (reversible, one interface, no restart)
// ---------------------------------------------------------------------------

static pid_t find_pid_by_name(const char *name) {
	DIR *d = opendir("/proc");
	if (!d) return -1;
	struct dirent *e;
	pid_t found = -1;
	while ((e = readdir(d))) {
		if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
		char path[64];
		snprintf(path, sizeof(path), "/proc/%.20s/comm", e->d_name);
		char comm[32];
		if (read_small_file(path, comm, sizeof(comm)) > 0 && !strcmp(comm, name)) {
			found = (pid_t)atoi(e->d_name);
			break;
		}
	}
	closedir(d);
	return found;
}

static int nm_reload(void) {
	pid_t pid = find_pid_by_name("NetworkManager");
	if (pid < 0) {
		vcam_log("wifi: NetworkManager not running, no scoping needed");
		return 0;
	}
	if (kill(pid, SIGHUP) < 0) {
		vcam_log("wifi: could not signal NetworkManager: %s", strerror(errno));
		return -1;
	}
	msleep(300);
	return 0;
}

static int nm_unmanage(const char *iface, char *dropin_out, size_t size) {
	// Never a wildcard: exactly this interface
	snprintf(dropin_out, size, WIFI_NM_DROPIN);
	FILE *f = fopen(dropin_out, "w");
	if (!f) {
		vcam_log("wifi: cannot write %s (%s)", dropin_out, strerror(errno));
		return -1;
	}
	fprintf(f, "# created by vcam wifi backend, auto-removed on teardown\n");
	fprintf(f, "[keyfile]\n");
	fprintf(f, "unmanaged-devices=interface-name:%s\n", iface);
	fclose(f);
	return nm_reload();
}

static void nm_restore(const char *dropin) {
	if (!file_exists(dropin)) return;
	unlink(dropin);
	nm_reload();
}

// ---------------------------------------------------------------------------
// Config generation
// ---------------------------------------------------------------------------

static int write_hostapd_conf(struct wifi_state *st) {
	char path[160];
	snprintf(path, sizeof(path), "%s/hostapd.conf", st->workdir);
	FILE *f = fopen(path, "w");
	if (!f) return -1;
	fprintf(f, "# generated by vcam wifi backend\n");
	fprintf(f, "interface=%s\n", st->cfg->iface);
	fprintf(f, "driver=nl80211\n");
	fprintf(f, "ssid=%s\n", st->ssid);
	fprintf(f, "channel=%s\n", st->channel);
	fprintf(f, "hw_mode=g\n");
	fprintf(f, "ieee80211n=1\n");
	fprintf(f, "wmm_enabled=1\n");
	fprintf(f, "auth_algs=1\n");
	if (st->password_eff[0]) {
		fprintf(f, "wpa=2\n");
		fprintf(f, "wpa_passphrase=%s\n", st->password_eff);
		fprintf(f, "wpa_key_mgmt=WPA-PSK\n");
		fprintf(f, "rsn_pairwise=CCMP\n");
	}
	fprintf(f, "ctrl_interface=%s\n", st->ctrl_dir);
	fprintf(f, "ctrl_interface_group=0\n");
	fclose(f);
	return 0;
}

// Can we bind DNS port 53 on this address? Probes UDP (dnsmasq's main
// socket) and TCP (its fallback socket) - both must be free. A system
// daemon holding 0.0.0.0:53 (AdGuard, unbound, a distro dnsmasq service,
// ...) makes a specific-address bind fail with EADDRINUSE.
static int port53_free(const char *ip) {
	struct sockaddr_in a;
	int ok = 1;

	memset(&a, 0, sizeof(a));
	a.sin_family = AF_INET;
	a.sin_port = htons(53);
	if (inet_pton(AF_INET, ip, &a.sin_addr) != 1) return 1;

	int fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd >= 0) {
		if (bind(fd, (struct sockaddr *)&a, sizeof(a)) < 0 && errno == EADDRINUSE) ok = 0;
		close(fd);
	}
	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd >= 0) {
		if (bind(fd, (struct sockaddr *)&a, sizeof(a)) < 0 && errno == EADDRINUSE) ok = 0;
		close(fd);
	}
	return ok;
}

static int write_dnsmasq_conf(struct wifi_state *st, int dns_ok) {
	char path[160];
	snprintf(path, sizeof(path), "%s/dnsmasq.conf", st->workdir);
	FILE *f = fopen(path, "w");
	if (!f) return -1;
	fprintf(f, "# generated by vcam wifi backend - no internet, like a real camera\n");
	if (!dns_ok) {
		// port 53 is owned by a system daemon on this host: answering
		// DNS is optional (the phone just sees "no internet", which is
		// what a camera AP looks like anyway) - DHCP is what matters
		fprintf(f, "port=0\n");
	}
	fprintf(f, "interface=%s\n", st->cfg->iface);
	fprintf(f, "bind-interfaces\n");
	fprintf(f, "no-resolv\n");
	fprintf(f, "no-hosts\n");
	fprintf(f, "user=root\n");
	fprintf(f, "group=root\n");
	fprintf(f, "dhcp-authoritative\n");
	fprintf(f, "dhcp-range=%s,%s,255.255.255.0,12h\n", st->dhcp_start, st->dhcp_end);
	fprintf(f, "dhcp-option=3,%s\n", st->gateway);	// router
	fprintf(f, "dhcp-option=6,%s\n", st->gateway);	// DNS = gateway (answers nothing)
	fprintf(f, "dhcp-leasefile=%s/dnsmasq.leases\n", st->workdir);
	fprintf(f, "pid-file=%s/dnsmasq.pid\n", st->workdir);
	fprintf(f, "log-facility=%s/dnsmasq.log\n", st->workdir);
	fclose(f);
	return 0;
}

// ---------------------------------------------------------------------------
// Child processes (PDEATHSIG: cannot outlive vcam)
// ---------------------------------------------------------------------------

// Kill daemons from previous runs whose configs carry our marker directory
static void reap_stale_daemons(void) {
	DIR *d = opendir("/proc");
	if (!d) return;
	struct dirent *e;
	pid_t pids[16];
	int count = 0;

	while ((e = readdir(d)) && count < 16) {
		if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
		char path[96];
		snprintf(path, sizeof(path), "/proc/%.20s/cmdline", e->d_name);
		int fd = open(path, O_RDONLY);
		if (fd < 0) continue;
		char cmd[512];
		ssize_t len = read(fd, cmd, sizeof(cmd) - 1);
		close(fd);
		if (len <= 0) continue;
		cmd[len] = 0;

		// argv is NUL-separated; the first arg decides which daemon
		if (strncmp(cmd, "hostapd", 7) && strncmp(cmd, "dnsmasq", 7)) continue;
		if (!strstr(cmd, WIFI_BASE_DIR)) continue;

		pid_t pid = (pid_t)atoi(e->d_name);
		if (pid == getpid()) continue;
		pids[count++] = pid;
	}
	closedir(d);

	for (int i = 0; i < count; i++) {
		vcam_log("wifi: killing stale daemon pid %d from a previous run", pids[i]);
		kill(pids[i], SIGTERM);
	}
	if (count) msleep(200);
	for (int i = 0; i < count; i++) kill(pids[i], SIGKILL);
}

static int find_daemon_path(const char *name, char *out, size_t size) {
	static const char *dirs[] = { "/usr/sbin", "/usr/bin", "/sbin", "/bin", "/usr/local/sbin", "/usr/local/bin" };
	for (unsigned i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
		snprintf(out, size, "%s/%s", dirs[i], name);
		if (file_exists(out)) return 0;
	}
	return -1;
}

static pid_t spawn_daemon(const char *path, char *const argv[], const char *logfile) {
	pid_t pid = fork();
	if (pid < 0) return -1;
	if (pid == 0) {
		// Die when vcam dies - no orphaned hostapd/dnsmasq, ever
		prctl(PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0);
		// race: parent may have died before prctl took effect
		if (getppid() == 1) _exit(1);

		int fd = open(logfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (fd >= 0) {
			dup2(fd, STDOUT_FILENO);
			dup2(fd, STDERR_FILENO);
			if (fd > STDERR_FILENO) close(fd);
		}
		execv(path, argv);
		_exit(1);
	}
	return pid;
}

static int child_alive(pid_t pid) {
	if (pid <= 0) return 0;
	return waitpid(pid, NULL, WNOHANG) == 0;
}

// ---------------------------------------------------------------------------
// hostapd control interface client
// ---------------------------------------------------------------------------

static void ctrl_drain_events(int fd);

static int ctrl_attach(struct wifi_state *st) {
	char path[256];
	snprintf(path, sizeof(path), "%s/%s", st->ctrl_dir, st->cfg->iface); // path[256]

	int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (fd < 0) {
		vcam_log("wifi: ctrl socket: %s", strerror(errno));
		return -1;
	}

	// hostapd replies from its own process: allow up to 2s for ATTACH/OK
	// instead of a racy non-blocking recv (MSG_DONTWAIT here failed
	// whenever the reply hadn't been scheduled in yet)
	struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

	struct sockaddr_un addr = {0};
	addr.sun_family = AF_UNIX;
	// abstract socket name keeps the filesystem clean
	snprintf(addr.sun_path + 1, sizeof(addr.sun_path) - 1, "vcam-wifi-%d", getpid());
	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		vcam_log("wifi: ctrl bind: %s", strerror(errno));
		close(fd);
		return -1;
	}

	struct sockaddr_un dst = {0};
	dst.sun_family = AF_UNIX;
	strncpy(dst.sun_path, path, sizeof(dst.sun_path) - 1);
	if (connect(fd, (struct sockaddr *)&dst, sizeof(dst)) < 0) {
		vcam_log("wifi: ctrl connect %s: %s", path, strerror(errno));
		close(fd);
		return -1;
	}

	// hostapd compares ctrl commands with os_strcmp: no trailing newline
	// ("ATTACH\n" != "ATTACH" -> "UNKNOWN COMMAND")
	if (send(fd, "ATTACH", 6, 0) < 0) {
		vcam_log("wifi: ctrl send: %s", strerror(errno));
		close(fd);
		return -1;
	}
	char reply[16] = {0};
	int n = recv(fd, reply, sizeof(reply) - 1, 0);
	if (n < 0) {
		vcam_log("wifi: no ATTACH reply from hostapd: %s", strerror(errno));
		close(fd);
		return -1;
	}
	reply[n] = 0;
	if (strncmp(reply, "OK", 2) != 0) {
		vcam_log("wifi: hostapd rejected ATTACH: '%s'", reply);
		close(fd);
		return -1;
	}
	return fd;
}

// Send a command and read the reply into out. Returns 0 or -1.
// hostapd's ctrl protocol is newline-free (os_strcmp on the exact buffer):
// queued event datagrams are drained first so the reply read belongs to
// this command; recv is bounded by the SO_RCVTIMEO from ctrl_attach.
static int ctrl_command(int fd, const char *cmd, char *out, int size) {
	ctrl_drain_events(fd);
	if (send(fd, cmd, strlen(cmd), 0) < 0) return -1;
	int n = recv(fd, out, size - 1, 0);
	if (n < 0) return -1;
	out[n] = 0;
	return 0;
}

// Read STATUS and extract a "key=" value (first match)
static int ctrl_status_get(int fd, const char *key, char *out, int size) {
	char status[2048];
	if (ctrl_command(fd, "STATUS", status, sizeof(status)) < 0) return -1;

	char *line = status;
	while (line && *line) {
		char *next = strchr(line, '\n');
		if (next) *next = 0;
		if (!strncmp(line, key, strlen(key)) && line[strlen(key)] == '=') {
			snprintf(out, size, "%s", line + strlen(key) + 1);
			return 0;
		}
		line = next ? next + 1 : NULL;
	}
	return -1;
}

// Drain queued hostapd event datagrams. Tracks association state: the
// AP-STA-CONNECTED/DISCONNECTED events are the reliable way to observe a
// phone joining (STATUS has no bare num_sta key). Uses the single-camera
// backend state (wifi_g) - the wifi backend is one camera per process.
static void ctrl_drain_events(int fd) {
	char buf[256];
	int n;
	// recv does not NUL-terminate: without it strstr/log lines pick up
	// stale bytes from previous events (seen as a 7-octet MAC)
	while ((n = (int)recv(fd, buf, sizeof(buf) - 1, MSG_DONTWAIT)) > 0) {
		buf[n] = 0;
		// events like "<3>AP-STA-CONNECTED aa:bb:.."
		char *p = buf;
		while (*p == '<' || (*p >= '0' && *p <= '9')) p++;
		if (strstr(p, "AP-STA-CONNECTED")) {
			if (wifi_g) wifi_g->sta_connected = 1;
			p[strcspn(p, "\n")] = 0;
			vcam_log("wifi: %s", p);
		} else if (strstr(p, "AP-STA-DISCONNECTED")) {
			if (wifi_g) wifi_g->sta_connected = 0;
			p[strcspn(p, "\n")] = 0;
			vcam_log("wifi: %s", p);
		}
	}
}

// ---------------------------------------------------------------------------
// Teardown (idempotent - safe from atexit, signals and --cleanup)
// ---------------------------------------------------------------------------

// Remove a directory tree rooted at path (our own workdir only)
static void rmtree(const char *path) {
	DIR *d = opendir(path);
	if (!d) return;
	struct dirent *e;
	char sub[1024];
	while ((e = readdir(d))) {
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
		snprintf(sub, sizeof(sub), "%.255s/%.255s", path, e->d_name);
		struct stat st;
		if (lstat(sub, &st) == 0) {
			if (S_ISDIR(st.st_mode)) rmtree(sub);
			else unlink(sub);
		}
	}
	closedir(d);
	rmdir(path);
}

static void wifi_teardown(struct wifi_state *st) {
	if (st == NULL || st->teardown_done) return;
	st->teardown_done = 1;

	// 1. children
	if (st->hostapd_pid > 0) kill(st->hostapd_pid, SIGTERM);
	if (st->dnsmasq_pid > 0) kill(st->dnsmasq_pid, SIGTERM);
	if (st->hostapd_pid > 0 || st->dnsmasq_pid > 0) msleep(150);
	if (st->hostapd_pid > 0) {
		kill(st->hostapd_pid, SIGKILL);
		waitpid(st->hostapd_pid, NULL, WNOHANG);
	}
	if (st->dnsmasq_pid > 0) {
		kill(st->dnsmasq_pid, SIGKILL);
		waitpid(st->dnsmasq_pid, NULL, WNOHANG);
	}
	if (st->hostapd_ctrl >= 0) close(st->hostapd_ctrl);

	// 2. NetworkManager scoping
	if (st->nm_dropin_created) nm_restore(st->nm_dropin);

	// 3. exactly the address we added (never flush)
	if (st->addr_added) {
		struct nlsock n;
		if (nl_open(&n, NETLINK_ROUTE) == 0) {
			rt_del_addr(&n, st->cfg->ifindex, st->gateway, 24);
			// bring the iface back down only if we brought it up
			if (!st->iface_was_up)
				rt_set_link_up(&n, st->cfg->ifindex, 0);
			nl_close(&n);
		}
	}

	// 4. our files
	if (st->workdir[0]) rmtree(st->workdir);
	unlink(WIFI_STATE_FILE);

	vcam_log("wifi: teardown complete");
}

static void wifi_atexit(void) {
	wifi_teardown(wifi_g);
}

static void wifi_signal_handler(int sig) {
	(void)sig;
	wifi_teardown(wifi_g);
	_exit(0);
}

// Persistent state for --cleanup after a crash (PDEATHSIG killed the
// daemons, but the NM drop-in / address can remain if the parent was
// SIGKILLed or the machine rebooted uncleanly)
static void write_state_file(struct wifi_state *st) {
	mkdir(WIFI_BASE_DIR, 0755);
	FILE *f = fopen(WIFI_STATE_FILE, "w");
	if (!f) return;
	fprintf(f, "iface=%s\n", st->cfg->iface);
	fprintf(f, "ifindex=%d\n", st->cfg->ifindex);
	fprintf(f, "addr=%s\n", st->addr_added ? st->gateway : "-");
	fprintf(f, "prefix=24\n");
	fprintf(f, "was_up=%d\n", st->iface_was_up);
	fprintf(f, "dropin=%s\n", st->nm_dropin_created ? st->nm_dropin : "-");
	fclose(f);
}

static void cleanup_from_state(void) {
	FILE *f = fopen(WIFI_STATE_FILE, "r");
	if (!f) {
		vcam_log("wifi: nothing to clean up (no state file)");
		return;
	}
	char line[256];
	char dropin[128] = "", addr[32] = "";
	int ifindex = 0, was_up = 0;
	while (fgets(line, sizeof(line), f)) {
		char *nl = strchr(line, '\n');
		if (nl) *nl = 0;
		char *eq = strchr(line, '=');
		if (!eq) continue;
		*eq = 0;
		if (!strcmp(line, "ifindex")) ifindex = atoi(eq + 1);
		else if (!strcmp(line, "addr") && strcmp(eq + 1, "-")) snprintf(addr, sizeof(addr), "%s", eq + 1);
		else if (!strcmp(line, "was_up")) was_up = atoi(eq + 1);
		else if (!strcmp(line, "dropin") && strcmp(eq + 1, "-")) snprintf(dropin, sizeof(dropin), "%s", eq + 1);
	}
	fclose(f);

	reap_stale_daemons();
	if (dropin[0]) nm_restore(dropin);

	if (addr[0] && ifindex > 0) {
		struct nlsock n;
		if (nl_open(&n, NETLINK_ROUTE) == 0) {
			rt_del_addr(&n, ifindex, addr, 24);
			if (!was_up) rt_set_link_up(&n, ifindex, 0);
			nl_close(&n);
		}
	}
	unlink(WIFI_STATE_FILE);
	vcam_log("wifi: cleanup complete");
}

// ---------------------------------------------------------------------------
// Preset resolution
// ---------------------------------------------------------------------------

struct subnet_check {
	const char *planned_net;	// e.g. "192.168.1"
	const char *shift_net;		// e.g. "192.168.77"
	int collide;
};

static void subnet_collision_cb(struct addr_entry *addr, void *arg) {
	struct subnet_check *sc = arg;
	if (sc->collide || addr->ip[0] == 0) return;
	size_t len = strlen(sc->planned_net);
	if (!strncmp(addr->ip, sc->planned_net, len) && addr->ip[len] == '.') {
		sc->collide = 1;
	}
}

// Resolve the AP identity (cam->ap_*, hardcoded per model in the vendor
// init functions, possibly CLI-overridden) into concrete values. Shifts the
// subnet when the planned range collides with an existing interface's range.
static int resolve_network(struct wifi_state *st, struct nlsock *n) {
	const char *gw_src = st->cam->ap_overridden ? "cli override" : st->cam->ap_gateway[0] ? "model init" : "generic default";

	// gateway
	if (st->cam->ap_gateway[0]) {
		snprintf(st->gateway, sizeof(st->gateway), "%s", st->cam->ap_gateway);
	} else {
		snprintf(st->gateway, sizeof(st->gateway), "192.168.0.1");
		vcam_log("wifi: no gateway hardcoded for this model, using generic %s", st->gateway);
	}
	(void)gw_src; // provenance is printed by print_plan from cam fields

	// the planned /24 prefix ("192.168.1")
	char net[32];
	snprintf(net, sizeof(net), "%.31s", st->gateway);
	char *last_dot = strrchr(net, '.');
	if (last_dot) *last_dot = 0;

	const char *shift_net = "192.168.77";
	struct subnet_check sc = { .planned_net = net, .shift_net = shift_net, .collide = 0 };
	rt_iter_addrs(n, subnet_collision_cb, &sc);
	if (sc.collide) {
		if (st->cam->ap_no_subnet_shift) {
			vcam_log("wifi: gateway subnet %s.0/24 is already in use; refusing to change it during BLE handover", net);
			return -1;
		}
		vcam_log("wifi: subnet %s.0/24 already exists on another interface, shifting to %s.0/24", net, shift_net);
		// keep the host part - copy it out first, composing in place
		// from a pointer into the same buffer is UB (self-overlapping
		// snprintf merged "192.168.1.2" into "192.168.7772")
		char host_g[16];
		snprintf(host_g, sizeof(host_g), "%s", strrchr(st->gateway, '.'));
		snprintf(st->gateway, sizeof(st->gateway), "%s%s", shift_net, host_g);
		snprintf(st->dhcp_start, sizeof(st->dhcp_start), "%.26s.10", shift_net);
		snprintf(st->dhcp_end, sizeof(st->dhcp_end), "%.25s.100", shift_net);
	} else {
		snprintf(st->dhcp_start, sizeof(st->dhcp_start), "%.27s.10", net);
		snprintf(st->dhcp_end, sizeof(st->dhcp_end), "%.26s.100", net);
	}

	if (st->cfg->channel > 0) {
		snprintf(st->channel, sizeof(st->channel), "%d", st->cfg->channel);
	} else {
		snprintf(st->channel, sizeof(st->channel), "6");
	}

	// password: cam->ap_password (empty = open); --password "" forces open
	if (st->cam->ap_password[0]) {
		snprintf(st->password_eff, sizeof(st->password_eff), "%s", st->cam->ap_password);
	} else {
		st->password_eff[0] = 0;
	}

	// ssid
	if (st->cam->ap_ssid[0]) {
		snprintf(st->ssid, sizeof(st->ssid), "%s", st->cam->ap_ssid);
	} else {
		// "fuji_x_h1" -> "VCAM-XH1-ABCD" style generic: strip the
		// "<vendor>_" prefix, drop underscores, uppercase
		char m[64];
		char src[64];
		snprintf(src, sizeof(src), "%s", st->cfg->model);
		char *p = src;
		while (*p && *p != '_') p++;
		if (*p == '_') p++;
		char *o = m;
		for (; *p; p++) {
			if (*p == '_') continue;
			if (*p >= 'a' && *p <= 'z') *p -= 32;
			*o++ = *p;
		}
		*o = 0;
		snprintf(st->ssid, sizeof(st->ssid), "VCAM-%.40s-ABCD", m);
		vcam_log("wifi: no ssid hardcoded for this model, using generic '%s'", st->ssid);
	}

	// gate: never let a malformed address reach netlink/hostapd/dnsmasq
	struct in_addr probe;
	if (inet_pton(AF_INET, st->gateway, &probe) != 1) {
		vcam_log("wifi: BUG - resolved gateway '%s' is not a valid IPv4 address", st->gateway);
		st->gateway[0] = 0; // caller refuses on empty gateway
	}
	return 0;
}

static void print_plan(struct wifi_state *st,
		       struct wifi_ifaces *ifs, const struct wifi_iface *pick,
		       const char *reg) {
	vcam_log("wifi: ----- bring-up plan (nothing changed yet) -----");
	for (int i = 0; i < ifs->count; i++) {
		vcam_log("wifi:   iface %s driver=%s phy_ap=%s default_route=%s station_connected=%s",
			ifs->ifs[i].name, ifs->ifs[i].driver,
			ifs->ifs[i].ap_capable ? "yes" : "no",
			ifs->ifs[i].is_default_route ? "yes" : "no",
			ifs->ifs[i].station_connected ? "yes" : "no");
	}
	for (int i = 0; i < ifs->seen_count; i++) {
		if (!ifs->seen[i].wifi)
			vcam_log("wifi:   (link %s: %s)", ifs->seen[i].name, ifs->seen[i].reason);
	}

	if (ifs->count == 0) {
		vcam_log("wifi: no wifi interfaces found - diagnostic checklist:");
		vcam_log("wifi:   1. ip link                 # is the wifi NIC listed? DOWN is fine");
		vcam_log("wifi:   2. rfkill list             # nothing should be blocked");
		vcam_log("wifi:   3. lspci -k | grep -A3 -iE 'network|wireless'   # driver bound?");
		vcam_log("wifi:   4. dmesg | grep -iE 'ath9k|iwlwifi|ath10k'      # firmware ok?");
	}

	if (pick == NULL) {
		vcam_log("wifi: no usable AP interface - see the interface list above");
		vcam_log("wifi: (down interfaces ARE eligible; only default-route/connected ones are excluded)");
		return;
	}
	vcam_log("wifi:   selected: %s (driver %s)%s", pick->name, pick->driver,
		st->cfg->iface[0] ? " (--wifi-dev)" : " (auto)");
	const char *ssid_src = st->cam->ap_overridden ? "--ssid flag"
			     : st->cam->ap_ssid[0] ? "model init"
			     : "generic default";
	const char *pass_src = st->cam->ap_overridden ? "--password flag"
			     : st->cam->ap_password[0] ? "model init"
			     : "open network";
	const char *gw_src = st->cam->ap_overridden ? "--gateway flag"
			   : st->cam->ap_gateway[0] ? "model init"
			   : "generic default";
	vcam_log("wifi:   ssid: '%s' (%s)  channel: %s", st->ssid, ssid_src, st->channel);
	vcam_log("wifi:   wpa: %s (%s)", st->password_eff[0] ? "WPA2-PSK CCMP" : "open", pass_src);
	vcam_log("wifi:   gateway: %s (%s)  dhcp: %s - %s (no internet, like a camera)",
		st->gateway, gw_src, st->dhcp_start, st->dhcp_end);
	vcam_log("wifi:   regdomain: %s%s", reg,
		st->cfg->reg_domain[0] ? " (will be set - global to the kernel)" : " (unchanged)");
	vcam_log("wifi:   NetworkManager: unmanaged-devices=interface-name:%s (reversible)", pick->name);
	vcam_log("wifi: ------------------------------------------------");
}

// ---------------------------------------------------------------------------
// Client wait state machine
// ---------------------------------------------------------------------------

static int wait_for_ctrl(struct wifi_state *st, int timeout_ms) {
	char path[256];
	snprintf(path, sizeof(path), "%s/%s", st->ctrl_dir, st->cfg->iface); // path[256]

	int waited = 0;
	while (!file_exists(path)) {
		if (!child_alive(st->hostapd_pid)) {
			vcam_log("wifi: hostapd exited early - log:");
			print_log_tail(st->hostapd_log, 12);
			return -1;
		}
		if (waited >= timeout_ms) return -1;
		msleep(200);
		waited += 200;
	}
	return 0;
}

// Wait until the client associated and has a lease. Returns 0, or -1 with
// the failure reason logged.
static int wait_for_client(struct wifi_state *st) {
	int fd = st->hostapd_ctrl;
	char val[64];
	int timeout_s = st->cfg->wait_timeout < 0 ? 120 : st->cfg->wait_timeout;

	// AP enabled: STATUS state=ENABLED (hostapd first line once
	// beaconing); fall back to the ssid key on builds without it
	int waited = 0;
	while (1) {
		ctrl_drain_events(fd);
		if (ctrl_status_get(fd, "state", val, sizeof(val)) == 0 &&
		    strcmp(val, "ENABLED") == 0) {
			vcam_log("wifi: AP enabled");
			break;
		}
		if (ctrl_status_get(fd, "ssid", val, sizeof(val)) == 0) {
			vcam_log("wifi: AP enabled, ssid '%s'", val);
			break;
		}
		if (!child_alive(st->hostapd_pid)) {
			vcam_log("wifi: hostapd exited - log:");
			print_log_tail(st->hostapd_log, 12);
			return -1;
		}
		if (waited >= 10000) {
			vcam_log("wifi: hostapd not enabled after 10s - log:");
			print_log_tail(st->hostapd_log, 12);
			return -1;
		}
		msleep(300);
		waited += 300;
	}

	// association: hostapd emits AP-STA-CONNECTED to attached sockets
	// (STATUS has no bare num_sta key to poll instead)
	waited = 0;
	while (1) {
		ctrl_drain_events(fd);
		if (st->sta_connected) {
			vcam_log("wifi: client associated");
			break;
		}
		if (!child_alive(st->hostapd_pid)) {
			vcam_log("wifi: hostapd exited - log:");
			print_log_tail(st->hostapd_log, 12);
			return -1;
		}
		if (timeout_s > 0 && waited >= timeout_s * 1000) {
			vcam_log("wifi: no client associated after %ds (is the SSID visible on the phone? check %s for channel/regdomain errors)", timeout_s, st->hostapd_log);
			return -1;
		}
		msleep(300);
		waited += 300;
	}

	// lease: dnsmasq lease file gains an entry
	waited = 0;
	int lease_timeout = timeout_s > 0 ? timeout_s * 1000 : 30000;
	while (1) {
		if (!child_alive(st->dnsmasq_pid)) {
			vcam_log("wifi: dnsmasq exited - log:");
			print_log_tail(st->dnsmasq_log, 12);
			return -1;
		}
		FILE *f = fopen(st->lease_file, "r");
		if (f) {
			char line[256];
			while (fgets(line, sizeof(line), f)) {
				// "<expiry> <mac> <ip> <hostname|*>"
				char mac[32] = "", ip[64] = "";
				if (sscanf(line, "%*d %31s %63s", mac, ip) == 2) {
					vcam_log("wifi: client %s leased %s", mac, ip);
					fclose(f);
					return 0;
				}
			}
			fclose(f);
		}
		if (waited >= lease_timeout) {
			vcam_log("wifi: client associated but no DHCP lease after %ds (some apps stall while probing for internet - retrying is fine)", lease_timeout / 1000);
			return -1;
		}
		msleep(300);
		waited += 300;
	}
}

// ---------------------------------------------------------------------------
// Entry points
// ---------------------------------------------------------------------------

int vcam_wifi_run(vcam *cam, int vendor) {
	static struct wifi_state st; // single-camera per process (like vhci)
	memset(&st, 0, sizeof(st));
	snprintf(wifi_cfg.model, sizeof(wifi_cfg.model), "%.63s", cam->model);
	st.cfg = &wifi_cfg;
	st.hostapd_ctrl = -1;
	wifi_g = &st;

	signal(SIGINT, wifi_signal_handler);
	signal(SIGTERM, wifi_signal_handler);
	atexit(wifi_atexit);

	reap_stale_daemons();

	if (st.cfg->cleanup_only) {
		cleanup_from_state();
		return 0;
	}

	struct nlsock n;
	if (nl_open(&n, NETLINK_ROUTE) < 0) {
		vcam_log("wifi: cannot open netlink socket: %s", strerror(errno));
		return -1;
	}

	struct nlsock gn;
	int family = -1;
	if (nl_open(&gn, NETLINK_GENERIC) == 0) {
		family = genl_resolve(&gn, "nl80211");
	}
	if (family < 0) {
		vcam_log("wifi: nl80211 family not available (kernel too old?)");
		nl_close(&n);
		return -1;
	}

	struct wifi_ifaces ifs;
	if (enum_wifi_ifaces(&n, &gn, family, &ifs) < 0) {
		nl_close(&gn);
		nl_close(&n);
		return -1;
	}
	const struct wifi_iface *pick = pick_ap_iface(&ifs, st.cfg->iface);
	if (pick == NULL) {
		if (ifs.count == 0 && ifs.seen_count > 0) {
			// links were seen but none were wifi - show what we saw
			vcam_log("wifi: links seen (none are wifi interfaces):");
			for (int i = 0; i < ifs.seen_count; i++) {
				vcam_log("wifi:   %s: %s", ifs.seen[i].name, ifs.seen[i].reason);
			}
		} else if (ifs.count == 0) {
			vcam_log("wifi: run with --check for the diagnostic checklist");
		}
		nl_close(&gn);
		nl_close(&n);
		return -1;
	}
	st.cfg->ifindex = pick->index;
	snprintf(st.cfg->iface, sizeof(st.cfg->iface), "%s", pick->name);
	st.cam = cam;

	if (resolve_network(&st, &n) < 0 || st.gateway[0] == 0) {
		vcam_log("wifi: cannot continue without a valid gateway address");
		nl_close(&gn);
		nl_close(&n);
		return -1;
	}

	char reg[4] = "";
	nl80211_get_regdomain(&gn, family, reg);

	if (st.cfg->check_only) {
		print_plan(&st, &ifs, pick, reg);
		nl_close(&gn);
		nl_close(&n);
		return 0;
	}

	vcam_log("wifi: using %s (driver %s), ssid '%s' channel %s, gateway %s",
		pick->name, pick->driver, st.ssid, st.channel, st.gateway);

	// regdomain: warn-only unless explicitly requested
	if (reg[0] && !strcmp(reg, "00") && st.cfg->reg_domain[0] == 0) {
		vcam_log("wifi: regdomain is 00 (world) - if the AP fails to start, try --reg-domain <CC> (e.g. US/DE)");
	}

	// --- stage: NetworkManager scoping (recorded, reversible) ---
	if (nm_unmanage(pick->name, st.nm_dropin, sizeof(st.nm_dropin)) == 0) {
		st.nm_dropin_created = 1;
	}

	// --- stage: iface up + address ---
	struct link_info li;
	int have = rt_get_link(&n, pick->name, &li);
	st.iface_was_up = (have == 0 && (li.flags & IFF_UP));
	if (rt_set_link_up(&n, pick->index, 1) < 0) {
		vcam_log("wifi: cannot bring %s up: %s (need root/CAP_NET_ADMIN)", pick->name, strerror(errno));
		nl_close(&gn);
		nl_close(&n);
		return -1;
	}

	// refuse if the planned gateway address exists on a different iface;
	// if it is already on our iface, do not add it twice
	{
		struct ifaddrmsg gen = {0};
		gen.ifa_family = AF_INET;
		nl_dump_msg(&n, RTM_GETADDR, &gen, sizeof(gen));
		struct addr_entry tmp;
		while (1) {
			struct nlmsghdr *nlh = NULL;
			ssize_t rc = nl_recv_one(&n, &nlh);
			if (rc <= 0) break;
			if (!addr_cb(nlh, &tmp)) continue;
			if (strcmp(tmp.ip, st.gateway)) continue;
			if (tmp.ifname[0] && strcmp(tmp.ifname, pick->name)) {
				vcam_log("wifi: address %s already exists on %s, refusing", st.gateway, tmp.ifname);
				nl_close(&gn);
				nl_close(&n);
				return -1;
			}
			vcam_log("wifi: %s already has %s, not adding again", pick->name, st.gateway);
			goto addr_done;
		}
	}
	if (rt_add_addr(&n, pick->index, st.gateway, 24) < 0) {
		vcam_log("wifi: cannot add %s/24 to %s: %s", st.gateway, pick->name, strerror(errno));
		nl_close(&gn);
		nl_close(&n);
		return -1;
	}
	st.addr_added = 1;
addr_done:

	// --- stage: configs ---
	mkdir(WIFI_BASE_DIR, 0755);
	snprintf(st.workdir, sizeof(st.workdir), WIFI_BASE_DIR "/run-%d", getpid());
	if (mkdir(st.workdir, 0755) < 0 && errno != EEXIST) {
		vcam_log("wifi: cannot create workdir %s: %s", st.workdir, strerror(errno));
		nl_close(&gn);
		nl_close(&n);
		return -1;
	}
	snprintf(st.hostapd_log, sizeof(st.hostapd_log), "%s/hostapd.log", st.workdir);
	snprintf(st.dnsmasq_log, sizeof(st.dnsmasq_log), "%s/dnsmasq.log", st.workdir);
	snprintf(st.lease_file, sizeof(st.lease_file), "%s/dnsmasq.leases", st.workdir);
	snprintf(st.ctrl_dir, sizeof(st.ctrl_dir), "%s/ctrl", st.workdir);

	int dns_ok = port53_free(st.gateway);
	if (!dns_ok) {
		vcam_log("wifi: port 53 already in use on %s (system DNS daemon?), disabling DNS - DHCP only", st.gateway);
	}
	if (write_hostapd_conf(&st) || write_dnsmasq_conf(&st, dns_ok)) {
		vcam_log("wifi: cannot write configs");
		nl_close(&gn);
		nl_close(&n);
		return -1;
	}

	// --- stage: regdomain (explicit only) ---
	if (st.cfg->reg_domain[0] && nl80211_set_regdomain(&gn, family, st.cfg->reg_domain) < 0) {
		vcam_log("wifi: setting regdomain %s failed: %s", st.cfg->reg_domain, strerror(errno));
	}

	write_state_file(&st);

	// --- stage: spawn hostapd ---
	char hostapd_path[128];
	if (find_daemon_path("hostapd", hostapd_path, sizeof(hostapd_path)) < 0) {
		vcam_log("wifi: hostapd not found (sudo apt install hostapd)");
		nl_close(&gn);
		nl_close(&n);
		return -1;
	}
	char conf[192];
	snprintf(conf, sizeof(conf), "%s/hostapd.conf", st.workdir);
	char *hostapd_argv[] = { hostapd_path, conf, NULL };
	st.hostapd_pid = spawn_daemon(hostapd_path, hostapd_argv, st.hostapd_log);
	if (st.hostapd_pid < 0) {
		vcam_log("wifi: cannot fork hostapd");
		nl_close(&gn);
		nl_close(&n);
		return -1;
	}

	if (wait_for_ctrl(&st, 10000) < 0) {
		nl_close(&gn);
		nl_close(&n);
		return -1;
	}
	st.hostapd_ctrl = ctrl_attach(&st);
	if (st.hostapd_ctrl < 0) {
		vcam_log("wifi: cannot attach to hostapd control interface");
		nl_close(&gn);
		nl_close(&n);
		return -1;
	}

	// --- stage: spawn dnsmasq ---
	char dnsmasq_path[128];
	if (find_daemon_path("dnsmasq", dnsmasq_path, sizeof(dnsmasq_path)) < 0) {
		vcam_log("wifi: dnsmasq not found (sudo apt install dnsmasq)");
		nl_close(&gn);
		nl_close(&n);
		return -1;
	}
	char dconf[240];
	// --conf-file is registered as OPTIONAL_ARGUMENT (has_arg=2): getopt
	// only accepts its value attached with '='; a separate word survives
	// parsing as a bare argument -> "junk found in command line"
	snprintf(dconf, sizeof(dconf), "--conf-file=%s/dnsmasq.conf", st.workdir);
	char *dnsmasq_argv[] = { dnsmasq_path, dconf, NULL };
	st.dnsmasq_pid = spawn_daemon(dnsmasq_path, dnsmasq_argv, st.dnsmasq_log);
	if (st.dnsmasq_pid < 0) {
		vcam_log("wifi: cannot fork dnsmasq");
		nl_close(&gn);
		nl_close(&n);
		return -1;
	}

	// --- stage: wait for a phone ---
	if (st.cfg->wait_timeout != 0) {
		if (wait_for_client(&st) < 0) {
			nl_close(&gn);
			nl_close(&n);
			return -1;
		}
	} else {
		vcam_log("wifi: skipping client wait (--wait-timeout 0)");
	}

	// --- hand over to the vendor TCP flow on the AP address ---
	cam->custom_ip_addr = strdup(st.gateway);
	vcam_log("wifi: starting PTP/IP on %s", st.gateway);
	nl_close(&gn);
	nl_close(&n);

	if (vendor == WIFI_VENDOR_CANON) {
		canon_ssdp_start(cam);
		return ptpip_generic_main(cam);
	}
	return fuji_wifi_main(cam);
}

// Extract wifi-engine flags, compact the rest in place for the vendor
// parsers. Identity flags (--ssid/--password/--gateway) are parsed by the
// vendor inits (vcam_parse_ap_args) so they work on every backend.
int vcam_wifi_filter_args(int argc, char **argv, struct vcam_wifi_config *cfg) {
	memset(cfg, 0, sizeof(*cfg));
	cfg->wait_timeout = -1; // preset default

	int out = 0;
	for (int i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "--wifi-dev") && i + 1 < argc) {
			snprintf(cfg->iface, sizeof(cfg->iface), "%s", argv[++i]);
		} else if (!strcmp(argv[i], "--channel") && i + 1 < argc) {
			cfg->channel = atoi(argv[++i]);
		} else if (!strcmp(argv[i], "--reg-domain") && i + 1 < argc) {
			snprintf(cfg->reg_domain, sizeof(cfg->reg_domain), "%s", argv[++i]);
		} else if (!strcmp(argv[i], "--wait-timeout") && i + 1 < argc) {
			cfg->wait_timeout = atoi(argv[++i]);
		} else if (!strcmp(argv[i], "--check")) {
			cfg->check_only = 1;
		} else if (!strcmp(argv[i], "--cleanup")) {
			cfg->cleanup_only = 1;
		} else {
			argv[out++] = argv[i];
		}
	}
	return out;
}
