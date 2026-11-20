/* localtalk.c: AppleTalk for the IIgs's serial ports.
 *
 * A IIgs does AppleTalk through LocalTalk on one of its two serial ports:
 * the firmware puts the SCC into SDLC mode and sends and receives LLAP
 * frames (scc.c hands them over here, one whole frame at a time).
 *
 * This file is the other end of that cable. It does what the LocalTalk
 * hardware and the other nodes on the cable would do within microseconds
 * (answer an RTS with a CTS, send an RTS before a directed frame and wait for
 * the IIgs's CTS, keep the gaps between frames), and it carries the frames
 * that matter to the outside as "LocalTalk over UDP" (LToUDP): each frame, with
 * a 4 byte sender id before it and without its checksum, as one UDP packet to
 * the multicast group 239.192.76.84, port 1954. Mini vMac, TashRouter,
 * MultiTalk and other programs speak LToUDP, so a IIgs in EmuBuddy shares a
 * LocalTalk network with them; a router such as TashRouter joins that network
 * to EtherTalk, where file servers live. No administrator rights are needed.
 *
 * The way the LLAP dialogs are kept (the queue, the RTS/CTS handshake with the
 * IIgs, the gaps) follows the LLAP port of GSport's ATBridge by Peter Neubauer
 * (GSport, 2013-2014, GPL v2 or later).
 */

#include "defc.h"
#include "localtalk.h"

#ifdef _WIN32
# include <winsock2.h>
# include <ws2tcpip.h>
# include <process.h>
typedef SOCKET lt_sock_t;
# define LT_BAD_SOCK	INVALID_SOCKET
# define lt_closesocket	closesocket
#else
# include <sys/types.h>
# include <sys/socket.h>
# include <netinet/in.h>
# include <arpa/inet.h>
# include <fcntl.h>
# include <unistd.h>
# include <errno.h>
typedef int lt_sock_t;
# define LT_BAD_SOCK	(-1)
# define lt_closesocket	close
#endif
#include <time.h>

#define LT_GROUP	"239.192.76.84"		/* the last two numbers spell "LT" */
#define LT_PORT		1954

#define LLAP_DDP_SHORT	0x01
#define LLAP_DDP_LONG	0x02
#define LLAP_ENQ	0x81
#define LLAP_ACK	0x82
#define LLAP_RTS	0x84
#define LLAP_CTS	0x85

#define LLAP_MIN	3
#define LLAP_MAX	603
#define LT_IDG		400		/* gap between dialogs, microseconds */
#define LT_IFG		200		/* longest gap inside a dialog */
#define LT_GAP_TOL	4

#define LT_QUEUE	64		/* frames waiting for the IIgs */
#define LT_SEEN_SECS	300		/* a node heard this long ago is still there */

int	g_appletalk = 1;		/* config: 0 = off, 1 = LocalTalk over UDP */

typedef struct {
	int	len;
	byte	data[LLAP_MAX];
} LtFrame;

enum { DLG_READY, DLG_GOT_CTS, DLG_WAIT_IDG };

static LtFrame	g_q[LT_QUEUE];
static int	g_q_head, g_q_len;
static int	g_dialog = DLG_READY;
static dword64	g_dialog_end, g_last_frame;	/* in cycles (about microseconds) */

static lt_sock_t g_sock = LT_BAD_SOCK;
static struct sockaddr_in g_group;
static byte	g_sender_id[4];
static int	g_open_failed;
static int	g_port = -1;			/* the serial port doing AppleTalk, -1 none */
static int	g_node;				/* the IIgs's node number (WR6) */
static int	g_node_settled;			/* it has sent data from that number */
static time_t	g_seen[256];			/* when each node was last heard */
static int	g_last_enq_node = -1;
static dword64	g_last_enq_time;
static word32	g_frames_out, g_frames_in;
static char	g_status[160];
static char	g_message[160];
static int	g_message_new;

static void
lt_say(const char *fmt, ...)
{
	va_list	ap;

	va_start(ap, fmt);
	vsnprintf(g_message, sizeof(g_message), fmt, ap);
	va_end(ap);
	g_message_new = 1;
	printf("%s\n", g_message);
}

/* A message for the status bar, once (0 if there is none). */
const char *
localtalk_take_message(void)
{
	if(!g_message_new) {
		return 0;
	}
	g_message_new = 0;
	return g_message;
}

/* ---- the queue of frames for the IIgs ---- */

static LtFrame *
q_peek(void)
{
	return g_q_len ? &g_q[g_q_head] : 0;
}

static void
q_pop(void)
{
	if(g_q_len) {
		g_q_head = (g_q_head + 1) % LT_QUEUE;
		g_q_len--;
	}
}

static void
q_put(int at_head, int dst, int src, int type, const byte *payload, int plen)
{
	LtFrame	*f;

	if(plen < 0 || plen > LLAP_MAX - 3) {
		return;
	}
	if(g_q_len >= LT_QUEUE) {
		if(!at_head) {
			return;			/* full: the newest is lost, as on a busy cable */
		}
		g_q_len--;			/* make room for a control frame */
	}
	if(at_head) {
		g_q_head = (g_q_head + LT_QUEUE - 1) % LT_QUEUE;
		f = &g_q[g_q_head];
	} else {
		f = &g_q[(g_q_head + g_q_len) % LT_QUEUE];
	}
	g_q_len++;
	f->data[0] = (byte)dst;
	f->data[1] = (byte)src;
	f->data[2] = (byte)type;
	if(plen) {
		memcpy(&f->data[3], payload, (size_t)plen);
	}
	f->len = plen + 3;
}

/* Something went wrong inside a dialog: drop what belongs to it. A dialog
 * begins with an RTS, so everything up to the next RTS goes. */
static void
dialog_reset(void)
{
	LtFrame	*f;

	g_dialog = DLG_READY;
	g_last_frame = 0;
	while((f = q_peek()) != 0 && f->data[2] != LLAP_RTS) {
		q_pop();
	}
}

/* ---- the UDP side ---- */

static void
lt_net_close(void)
{
	if(g_sock != LT_BAD_SOCK) {
		lt_closesocket(g_sock);
		g_sock = LT_BAD_SOCK;
	}
}

static int
lt_net_open(void)
{
	struct sockaddr_in	local;
	struct ip_mreq		mreq;
	int			one = 1;
	unsigned char		ttl = 1, loop = 1;
	word32			pid;

	if(g_sock != LT_BAD_SOCK) {
		return 1;
	}
	if(g_open_failed) {
		return 0;
	}
#ifdef _WIN32
	{
		WSADATA wsa;

		WSAStartup(MAKEWORD(2, 2), &wsa);
	}
	pid = (word32)_getpid();
#else
	pid = (word32)getpid();
#endif
	g_sender_id[0] = (byte)(pid >> 24);
	g_sender_id[1] = (byte)(pid >> 16);
	g_sender_id[2] = (byte)(pid >> 8);
	g_sender_id[3] = (byte)pid;

	g_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if(g_sock == LT_BAD_SOCK) {
		g_open_failed = 1;
		return 0;
	}
	/* several programs on one computer share the port */
	setsockopt(g_sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof(one));
#ifdef SO_REUSEPORT
	setsockopt(g_sock, SOL_SOCKET, SO_REUSEPORT, (const char *)&one, sizeof(one));
#endif
	memset(&local, 0, sizeof(local));
	local.sin_family = AF_INET;
	local.sin_addr.s_addr = htonl(INADDR_ANY);
	local.sin_port = htons(LT_PORT);
	if(bind(g_sock, (struct sockaddr *)&local, sizeof(local)) < 0) {
		lt_net_close();
		g_open_failed = 1;
		return 0;
	}
	memset(&g_group, 0, sizeof(g_group));
	g_group.sin_family = AF_INET;
	g_group.sin_addr.s_addr = inet_addr(LT_GROUP);
	g_group.sin_port = htons(LT_PORT);

	memset(&mreq, 0, sizeof(mreq));
	mreq.imr_multiaddr.s_addr = inet_addr(LT_GROUP);
	mreq.imr_interface.s_addr = htonl(INADDR_ANY);
	/* without a network this fails; the socket still reaches programs on
	 * this computer where the system allows it */
	(void)setsockopt(g_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char *)&mreq,
								sizeof(mreq));
	/* stay in the local network; let other programs here listen in */
	setsockopt(g_sock, IPPROTO_IP, IP_MULTICAST_TTL, (const char *)&ttl, sizeof(ttl));
	setsockopt(g_sock, IPPROTO_IP, IP_MULTICAST_LOOP, (const char *)&loop, sizeof(loop));
#ifdef _WIN32
	{
		u_long nb = 1;

		ioctlsocket(g_sock, FIONBIO, &nb);
	}
#else
	fcntl(g_sock, F_SETFL, fcntl(g_sock, F_GETFL, 0) | O_NONBLOCK);
#endif
	return 1;
}

static void
lt_net_send(const byte *frame, int len)
{
	byte	pkt[4 + LLAP_MAX];

	if(len < LLAP_MIN || len > LLAP_MAX || !lt_net_open()) {
		return;
	}
	memcpy(pkt, g_sender_id, 4);
	memcpy(pkt + 4, frame, (size_t)len);
	if(sendto(g_sock, (const char *)pkt, (size_t)(len + 4), 0,
			(struct sockaddr *)&g_group, sizeof(g_group)) >= 0) {
		g_frames_out++;
	}
}

static int
node_in_use(int node)
{
	return g_seen[node & 0xff] != 0 &&
			(time(NULL) - g_seen[node & 0xff]) < LT_SEEN_SECS;
}

/* A frame from the network (without the sender id). */
static void
lt_net_frame(const byte *f, int len)
{
	int	dst = f[0], src = f[1], type = f[2];

	g_frames_in++;
	switch(type) {
	case LLAP_ENQ:
		/* someone asks whether a number is free. If it is the IIgs's, the
		 * IIgs answers for itself. */
		if(dst == g_node && g_node != 0) {
			q_put(0, dst, src, type, 0, 0);
		}
		break;
	case LLAP_ACK:
		/* someone has that number */
		g_seen[src] = time(NULL);
		if(dst == g_node && !g_node_settled) {
			q_put(1, dst, src, type, 0, 0);
		}
		break;
	case LLAP_DDP_SHORT:
	case LLAP_DDP_LONG:
		g_seen[src] = time(NULL);
		if(dst == 0xff) {
			q_put(0, dst, src, type, f + 3, len - 3);
		} else if(dst == g_node && g_node != 0) {
			/* a directed frame comes after an RTS, when the IIgs
			 * has answered with a CTS */
			q_put(0, dst, src, LLAP_RTS, 0, 0);
			q_put(0, dst, src, type, f + 3, len - 3);
		}
		break;
	default:
		/* RTS and CTS stay on each side's own end of the cable */
		break;
	}
}

static void
lt_net_poll(void)
{
	byte	pkt[4 + LLAP_MAX + 64];
	int	n, tries;

	if(g_sock == LT_BAD_SOCK) {
		return;
	}
	for(tries = 0; tries < 16; tries++) {
		n = (int)recvfrom(g_sock, (char *)pkt, sizeof(pkt), 0, 0, 0);
		if(n <= 0) {
			break;
		}
		if(n < 4 + LLAP_MIN || n > 4 + LLAP_MAX) {
			continue;
		}
		if(!memcmp(pkt, g_sender_id, 4)) {
			continue;			/* our own packet */
		}
		lt_net_frame(pkt + 4, n - 4);
	}
}

/* ---- the serial port side (scc.c) ---- */

/* The firmware has put a serial port into SDLC mode: it does AppleTalk. */
void
localtalk_port_on(int port)
{
	if(g_port == port) {
		return;
	}
	g_port = port;
	g_q_head = g_q_len = 0;
	g_dialog = DLG_READY;
	g_last_frame = 0;
	g_node = 0;
	g_node_settled = 0;
	g_open_failed = 0;
	if(lt_net_open()) {
		lt_say("AppleTalk on the %s port", port ? "modem" : "printer");
	} else {
		lt_say("AppleTalk: can't open the network (UDP port %d)", LT_PORT);
	}
}

void
localtalk_port_off(int port)
{
	if(g_port != port) {
		return;
	}
	g_port = -1;
	g_node = 0;
	g_node_settled = 0;
	lt_net_close();
}

void
localtalk_shutdown(void)
{
	g_port = -1;
	lt_net_close();
}

/* The node number the firmware listens for (WR6 of the SCC). */
void
localtalk_set_node(int node)
{
	if(node != g_node) {
		g_node = node & 0xff;
		g_node_settled = 0;
	}
}

/* One LLAP frame the IIgs has sent (destination, source, type, data). */
void
localtalk_from_gs(dword64 dfcyc, const byte *f, int len)
{
	dword64	now = dfcyc >> 16;
	int	dst, src, type;

	if(len < LLAP_MIN || len > LLAP_MAX) {
		return;
	}
	dst = f[0];
	src = f[1];
	type = f[2];
	g_last_frame = now;

	switch(type) {
	case LLAP_DDP_SHORT:
	case LLAP_DDP_LONG:
		if(!g_node_settled && src != 0) {
			g_node = src;
			g_node_settled = 1;
			lt_say("AppleTalk: the IIgs is node %d", src);
		}
		lt_net_send(f, len);
		break;
	case LLAP_ENQ:
		/* "is this number free?" It must be a user number (1-127) nobody
		 * here is known to have; and the network is asked as well. */
		if(dst == 0 || dst > 127 || node_in_use(dst)) {
			q_put(1, dst, dst, LLAP_ACK, 0, 0);
		}
		if(dst != g_last_enq_node || (now - g_last_enq_time) >= 5000) {
			g_last_enq_node = dst;
			g_last_enq_time = now;
			lt_net_send(f, len);
		}
		break;
	case LLAP_ACK:
		lt_net_send(f, len);		/* the IIgs defends its number */
		break;
	case LLAP_RTS:
		if(dst != 0xff) {
			q_put(1, src, dst, LLAP_CTS, 0, 0);
		}
		break;
	case LLAP_CTS:
		g_dialog = DLG_GOT_CTS;
		break;
	default:
		dialog_reset();
	}
}

/* The next LLAP frame for the IIgs, if it is time for one: returns its length
 * with two checksum bytes at the end (the SCC shows them to the firmware, which
 * does not check them), or 0. */
int
localtalk_to_gs(dword64 dfcyc, byte *buf, int max)
{
	dword64	now = dfcyc >> 16;
	LtFrame	*f;
	int	type, len, control, go;

	lt_net_poll();

	if(g_dialog == DLG_WAIT_IDG) {
		if((now - g_dialog_end) < LT_IDG) {
			return 0;
		}
		g_dialog = DLG_READY;
	}
	if(g_dialog != DLG_READY && g_last_frame != 0 &&
			(now - g_last_frame) >= (LT_GAP_TOL * LT_IFG)) {
		dialog_reset();			/* the IIgs did not go on in time */
	}
	f = q_peek();
	if(!f) {
		return 0;
	}
	type = f->data[2];
	control = (type & 0x80) != 0;
	go = 0;
	if(control) {
		go = 1;
	} else if(f->data[0] == 0xff) {
		go = (g_dialog == DLG_READY);		/* broadcasts need no CTS */
	} else {
		go = (g_dialog == DLG_GOT_CTS);		/* directed: after the CTS */
		if(!go && g_dialog == DLG_READY && (g_last_frame == 0 ||
				(now - g_last_frame) >= (LT_GAP_TOL * LT_IDG))) {
			q_pop();		/* the IIgs sent no CTS for it: lost */
			return 0;
		}
	}
	if(!go) {
		return 0;
	}
	len = f->len;
	if(len + 2 > max) {
		q_pop();
		return 0;
	}
	memcpy(buf, f->data, (size_t)len);
	buf[len] = 0xff;
	buf[len + 1] = 0xff;
	q_pop();
	if(control) {
		/* after an RTS the IIgs must answer; other control frames end here */
		g_dialog = DLG_READY;
	} else {
		g_dialog = DLG_WAIT_IDG;
		g_dialog_end = now;
	}
	g_last_frame = now;
	return len + 2;
}

int
localtalk_port(void)
{
	return g_port;
}

/* A line for the Slots window. */
const char *
localtalk_status(void)
{
	int	i, others = 0;

	if(!g_appletalk) {
		return "AppleTalk: off";
	}
	if(g_port < 0) {
		return "AppleTalk: starts when the IIgs uses it";
	}
	if(g_sock == LT_BAD_SOCK) {
		snprintf(g_status, sizeof(g_status),
			"AppleTalk: can't open the network (UDP port %d)", LT_PORT);
		return g_status;
	}
	for(i = 1; i < 255; i++) {
		if(i != g_node && node_in_use(i)) {
			others++;
		}
	}
	if(g_node_settled) {
		snprintf(g_status, sizeof(g_status),
			"AppleTalk: %s port, node %d, %d other%s heard",
			g_port ? "modem" : "printer", g_node, others,
			others == 1 ? "" : "s");
	} else {
		snprintf(g_status, sizeof(g_status),
			"AppleTalk: %s port, looking for a node number",
			g_port ? "modem" : "printer");
	}
	return g_status;
}
