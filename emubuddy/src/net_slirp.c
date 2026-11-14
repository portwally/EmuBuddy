/* EmuBuddy network backend: libslirp user-mode NAT. The emulated card's raw
 * Ethernet frames go into a private network (192.168.64.0/24) that slirp turns
 * into ordinary host sockets, so no root rights, pcap or entitlements are
 * needed. The network matches the one Ample/MAME uses with vmnet, so IIgs
 * disks already set up for Ample work unchanged:
 *   network 192.168.64.0/24, gateway and DNS 192.168.64.1,
 *   DHCP hands out addresses from 192.168.64.2; static addresses in the
 *   subnet (for example 192.168.64.116) work too.
 * The computer EmuBuddy runs on is 192.168.64.254 (NET_HOST_ADDR): a
 * connection from the IIgs to that address arrives at the computer's own
 * 127.0.0.1, so the IIgs reaches servers on this computer (the web gateway,
 * a test web server) at an address that never changes.
 *   Why not .1: slirp sends TCP for its DNS address to the real DNS server
 *   (usually the router), and the DNS address must stay .1 for existing
 *   disks. So slirp's "host" is .254 and its "name server" is .1; slirp
 *   answers ARP for both, and routes whatever a guest sends to either.
 * Everything runs on the emulator thread from uthernet2_poll(). */
#include "uthernet2.h"
#include <libslirp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define poll WSAPoll
typedef WSAPOLLFD Pfd;
#else
#include <poll.h>
#include <arpa/inet.h>
typedef struct pollfd Pfd;
#endif

/* libslirp before 4.9 (Ubuntu 24.04 has 4.7) knows no slirp_os_socket and no
 * "_socket" calls: its poll functions take plain int descriptors. */
#if !SLIRP_CHECK_VERSION(4, 9, 0)
#define NET_OLD_SLIRP	1
typedef int slirp_os_socket;
#define slirp_pollfds_fill_socket	slirp_pollfds_fill
#endif

static Slirp	*g_slirp;

#define MAX_FDS	256
static Pfd	g_fds[MAX_FDS];
static int	g_nfds;

typedef struct {
	SlirpTimerId	id;
	void		*cb_opaque;
	int64_t		expire_ms;
	int		armed;
	int		used;
} Timer;
#define MAX_TIMERS	8
static Timer	g_timers[MAX_TIMERS];

static int64_t
now_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static slirp_ssize_t
cb_send(const void *buf, size_t len, void *opaque)
{
	(void)opaque;
	uthernet2_rx_frame((const uint8_t *)buf, (int)len);
	return (slirp_ssize_t)len;
}

static void
cb_guest_error(const char *msg, void *opaque)
{
	(void)opaque;
	printf("slirp: guest error: %s\n", msg);
}

static int64_t
cb_clock(void *opaque)
{
	(void)opaque;
	return now_ns();
}

static void *
cb_timer_new_opaque(SlirpTimerId id, void *cb_opaque, void *opaque)
{
	int	i;
	(void)opaque;

	for(i = 0; i < MAX_TIMERS; i++) {
		if(!g_timers[i].used) {
			g_timers[i].used = 1;
			g_timers[i].armed = 0;
			g_timers[i].id = id;
			g_timers[i].cb_opaque = cb_opaque;
			return &g_timers[i];
		}
	}
	return NULL;
}

static void
cb_timer_free(void *timer, void *opaque)
{
	(void)opaque;
	if(timer) {
		((Timer *)timer)->used = 0;
	}
}

static void
cb_timer_mod(void *timer, int64_t expire_ms, void *opaque)
{
	(void)opaque;
	if(timer) {
		((Timer *)timer)->expire_ms = expire_ms;
		((Timer *)timer)->armed = 1;
	}
}

#ifndef NET_OLD_SLIRP
static void
cb_noop_socket(slirp_os_socket s, void *opaque)
{
	(void)s;
	(void)opaque;
}
#endif

/* Older callback form; slirp may still call it for some config versions. */
static void
cb_noop_fd(int fd, void *opaque)
{
	(void)fd;
	(void)opaque;
}

static void
cb_notify(void *opaque)
{
	(void)opaque;
}

static int
cb_add_poll(slirp_os_socket fd, int events, void *opaque)
{
	(void)opaque;
	if(g_nfds >= MAX_FDS) {
		return -1;
	}
	g_fds[g_nfds].fd = fd;
	g_fds[g_nfds].events = 0;
	if(events & SLIRP_POLL_IN) g_fds[g_nfds].events |= POLLIN;
	if(events & SLIRP_POLL_OUT) g_fds[g_nfds].events |= POLLOUT;
	if(events & SLIRP_POLL_PRI) g_fds[g_nfds].events |= POLLPRI;
	g_fds[g_nfds].revents = 0;
	return g_nfds++;
}

static int
cb_get_revents(int idx, void *opaque)
{
	int	r = 0, ev;
	(void)opaque;

	if(idx < 0 || idx >= g_nfds) {
		return 0;
	}
	ev = g_fds[idx].revents;
	if(ev & POLLIN) r |= SLIRP_POLL_IN;
	if(ev & POLLOUT) r |= SLIRP_POLL_OUT;
	if(ev & POLLPRI) r |= SLIRP_POLL_PRI;
	if(ev & POLLERR) r |= SLIRP_POLL_ERR;
	if(ev & POLLHUP) r |= SLIRP_POLL_HUP;
	return r;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
static const SlirpCb g_cb = {
	.send_packet = cb_send,
	.guest_error = cb_guest_error,
	.clock_get_ns = cb_clock,
	.timer_free = cb_timer_free,
	.timer_mod = cb_timer_mod,
	.notify = cb_notify,
	.timer_new_opaque = cb_timer_new_opaque,
#ifndef NET_OLD_SLIRP
	.register_poll_socket = cb_noop_socket,
	.unregister_poll_socket = cb_noop_socket,
#endif
	.register_poll_fd = cb_noop_fd,
	.unregister_poll_fd = cb_noop_fd,
};
#pragma GCC diagnostic pop

int
net_init(void)
{
	SlirpConfig	cfg;

	if(g_slirp) {
		return 0;
	}
	memset(&cfg, 0, sizeof(cfg));
	cfg.version = 4;
	cfg.restricted = 0;
	cfg.in_enabled = true;
	inet_pton(AF_INET, "192.168.64.0", &cfg.vnetwork);
	inet_pton(AF_INET, "255.255.255.0", &cfg.vnetmask);
	inet_pton(AF_INET, NET_HOST_ADDR, &cfg.vhost);
	inet_pton(AF_INET, "192.168.64.2", &cfg.vdhcp_start);
	inet_pton(AF_INET, "192.168.64.1", &cfg.vnameserver);
	cfg.in6_enabled = false;
	cfg.vhostname = "emubuddy";
	cfg.if_mtu = 1500;
	cfg.if_mru = 1500;
	g_slirp = slirp_new(&cfg, &g_cb, NULL);
	if(!g_slirp) {
		printf("slirp: could not start the network\n");
		return -1;
	}
	printf("slirp: network up (192.168.64.0/24, gateway and DNS "
				"192.168.64.1, DHCP from 192.168.64.2, this computer "
				"is " NET_HOST_ADDR ")\n");
	return 0;
}

void
net_send(const uint8_t *frame, int len)
{
	if(g_slirp) {
		slirp_input(g_slirp, frame, len);
	}
}

void
net_poll(void)
{
	uint32_t	timeout = 0;
	int		ret, i;
	int64_t		ms;

	if(!g_slirp) {
		return;
	}
	g_nfds = 0;
	slirp_pollfds_fill_socket(g_slirp, &timeout, cb_add_poll, NULL);
	ret = g_nfds ? poll(g_fds, (unsigned)g_nfds, 0) : 0;
	slirp_pollfds_poll(g_slirp, ret < 0, cb_get_revents, NULL);

	ms = now_ns() / 1000000;
	for(i = 0; i < MAX_TIMERS; i++) {
		if(g_timers[i].used && g_timers[i].armed &&
					ms >= g_timers[i].expire_ms) {
			g_timers[i].armed = 0;
			slirp_handle_timer(g_slirp, g_timers[i].id,
						g_timers[i].cb_opaque);
		}
	}
}

void
net_shutdown(void)
{
	if(g_slirp) {
		slirp_cleanup(g_slirp);
		g_slirp = NULL;
	}
}

const char *
net_backend_name(void)
{
	return "slirp (NAT)";
}
