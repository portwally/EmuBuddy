/* AppleTalk test: drives localtalk.c the way scc.c does (whole LLAP frames
 * from and to the IIgs) and plays a second node on the network with a UDP
 * socket of its own (LocalTalk over UDP, group 239.192.76.84, port 1954):
 *   1. an RTS from the IIgs is answered with a CTS at once, on the spot
 *   2. the IIgs asks for a node number: one outside 1-127 is refused, a free
 *      one is asked for on the network, one a node answers for is refused
 *   3. a frame the IIgs sends arrives on the network, sender id before it
 *   4. a broadcast from the network reaches the IIgs, checksum bytes added
 *   5. a directed frame from the network: first an RTS, the data only after
 *      the IIgs's CTS; without a CTS it is dropped
 *   6. a frame for another node does not reach the IIgs
 * Needs a computer that delivers its own multicast packets to itself (any
 * with a network) and no other LocalTalk-over-UDP program running, whose
 * packets would get in between. Exit code 0 when everything passes.
 *   cmake --build build --target test_localtalk && build/test_localtalk */
#include "../defc.h"
#include "../localtalk.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static int g_fail;
#define CHECK(c, msg) do { if(c) printf("  ok   %s\n", msg); \
	else { printf("  FAIL %s\n", msg); g_fail++; } } while(0)

static int	g_peer = -1;
static struct sockaddr_in g_group;
static const byte PEER_ID[4] = { 0x54, 0x45, 0x53, 0x54 };	/* "TEST" */
static dword64	g_now = 1000000;			/* cycles, about microseconds */

static dword64 dfcyc(void) { return g_now << 16; }

static void
peer_open(void)
{
	struct sockaddr_in	local;
	struct ip_mreq		mreq;
	int			one = 1;
	unsigned char		ttl = 1, loop = 1;

	g_peer = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	setsockopt(g_peer, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#ifdef SO_REUSEPORT
	setsockopt(g_peer, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));
#endif
	memset(&local, 0, sizeof(local));
	local.sin_family = AF_INET;
	local.sin_addr.s_addr = htonl(INADDR_ANY);
	local.sin_port = htons(1954);
	if(bind(g_peer, (struct sockaddr *)&local, sizeof(local)) < 0) {
		perror("bind");
		exit(2);
	}
	memset(&g_group, 0, sizeof(g_group));
	g_group.sin_family = AF_INET;
	g_group.sin_addr.s_addr = inet_addr("239.192.76.84");
	g_group.sin_port = htons(1954);
	memset(&mreq, 0, sizeof(mreq));
	mreq.imr_multiaddr.s_addr = inet_addr("239.192.76.84");
	mreq.imr_interface.s_addr = htonl(INADDR_ANY);
	setsockopt(g_peer, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
	setsockopt(g_peer, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
	setsockopt(g_peer, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));
	fcntl(g_peer, F_SETFL, fcntl(g_peer, F_GETFL, 0) | O_NONBLOCK);
}

static void
peer_send(const byte *frame, int len)
{
	byte	pkt[700];

	memcpy(pkt, PEER_ID, 4);
	memcpy(pkt + 4, frame, (size_t)len);
	sendto(g_peer, pkt, (size_t)(len + 4), 0, (struct sockaddr *)&g_group,
							sizeof(g_group));
	usleep(20000);			/* let the packet come round */
}

/* The next frame on the network that is not the peer's own; 0 if none. */
static int
peer_recv(byte *frame, int max)
{
	byte	pkt[700];
	int	n, tries;

	for(tries = 0; tries < 50; tries++) {
		n = (int)recvfrom(g_peer, pkt, sizeof(pkt), 0, 0, 0);
		if(n < 7) {
			usleep(2000);
			continue;
		}
		if(!memcmp(pkt, PEER_ID, 4)) {
			continue;
		}
		if(n - 4 > max) {
			return 0;
		}
		memcpy(frame, pkt + 4, (size_t)(n - 4));
		return n - 4;
	}
	return 0;
}

static void peer_drain(void) { byte b[700]; while(peer_recv(b, sizeof(b))) { } }

/* What the IIgs gets next (the gaps between frames are waited out). */
static int
gs_recv(byte *buf, int max)
{
	int	n, i;

	for(i = 0; i < 4; i++) {
		n = localtalk_to_gs(dfcyc(), buf, max);
		if(n) {
			return n;
		}
		g_now += 500;
	}
	return 0;
}

int
main(void)
{
	byte	f[700], buf[700];
	int	n, i;

	peer_open();
	localtalk_port_on(1);
	printf("%s\n", localtalk_status());
	CHECK(localtalk_port() == 1, "AppleTalk runs on the modem port");
	peer_drain();

	printf("1. RTS and CTS\n");
	localtalk_set_node(0x22);
	f[0] = 0x55; f[1] = 0x22; f[2] = 0x84;			/* RTS to node $55 */
	localtalk_from_gs(dfcyc(), f, 3);
	n = localtalk_to_gs(dfcyc(), buf, sizeof(buf));
	CHECK(n == 5 && buf[0] == 0x22 && buf[1] == 0x55 && buf[2] == 0x85,
						"the CTS comes back at once");
	CHECK(n == 5 && buf[3] == 0xff && buf[4] == 0xff, "with two checksum bytes");
	f[0] = 0xff;						/* RTS to everyone */
	localtalk_from_gs(dfcyc(), f, 3);
	CHECK(localtalk_to_gs(dfcyc(), buf, sizeof(buf)) == 0,
					"a broadcast RTS gets no CTS");
	CHECK(peer_recv(buf, sizeof(buf)) == 0, "RTS and CTS stay off the network");

	printf("2. Asking for a node number\n");
	g_now += 10000;
	f[0] = 200; f[1] = 200; f[2] = 0x81;			/* ENQ for 200 */
	localtalk_from_gs(dfcyc(), f, 3);
	n = gs_recv(buf, sizeof(buf));
	CHECK(n == 5 && buf[0] == 200 && buf[2] == 0x82, "200 is refused (not a user number)");
	peer_drain();
	g_now += 10000;
	localtalk_set_node(0x33);
	f[0] = 0x33; f[1] = 0x33; f[2] = 0x81;			/* ENQ for $33 */
	localtalk_from_gs(dfcyc(), f, 3);
	CHECK(gs_recv(buf, sizeof(buf)) == 0, "a free number is not refused");
	n = peer_recv(buf, sizeof(buf));
	CHECK(n == 3 && buf[0] == 0x33 && buf[2] == 0x81, "the network is asked");
	f[0] = 0x33; f[1] = 0x33; f[2] = 0x82;			/* the peer has $33 */
	peer_send(f, 3);
	n = gs_recv(buf, sizeof(buf));
	CHECK(n == 5 && buf[0] == 0x33 && buf[2] == 0x82, "a node's answer reaches the IIgs");
	g_now += 10000;
	f[2] = 0x81;
	localtalk_from_gs(dfcyc(), f, 3);
	n = gs_recv(buf, sizeof(buf));
	CHECK(n == 5 && buf[2] == 0x82, "and the number is refused from then on");
	peer_drain();

	printf("3. A frame from the IIgs\n");
	g_now += 10000;
	localtalk_set_node(0x22);
	f[0] = 0xff; f[1] = 0x22; f[2] = 0x01;
	for(i = 3; i < 40; i++) f[i] = (byte)i;
	localtalk_from_gs(dfcyc(), f, 40);
	n = peer_recv(buf, sizeof(buf));
	CHECK(n == 40 && !memcmp(buf, f, 40), "arrives on the network, byte for byte");

	printf("4. A broadcast from the network\n");
	g_now += 10000;
	f[0] = 0xff; f[1] = 0x55; f[2] = 0x01;
	peer_send(f, 30);
	n = gs_recv(buf, sizeof(buf));
	CHECK(n == 32 && !memcmp(buf, f, 30), "reaches the IIgs");
	CHECK(n == 32 && buf[30] == 0xff && buf[31] == 0xff, "with two checksum bytes");

	printf("5. A directed frame from the network\n");
	g_now += 10000;
	f[0] = 0x22; f[1] = 0x55; f[2] = 0x02;
	peer_send(f, 50);
	n = gs_recv(buf, sizeof(buf));
	CHECK(n == 5 && buf[0] == 0x22 && buf[1] == 0x55 && buf[2] == 0x84,
						"first an RTS for the IIgs");
	CHECK(localtalk_to_gs(dfcyc(), buf, sizeof(buf)) == 0, "no data before the CTS");
	f[0] = 0x55; f[1] = 0x22; f[2] = 0x85;			/* the IIgs's CTS */
	g_now += 100;
	localtalk_from_gs(dfcyc(), f, 3);
	n = localtalk_to_gs(dfcyc(), buf, sizeof(buf));
	CHECK(n == 52 && buf[0] == 0x22 && buf[2] == 0x02, "then the data");
	g_now += 10000;
	f[0] = 0x22; f[1] = 0x55; f[2] = 0x02;
	peer_send(f, 50);
	n = gs_recv(buf, sizeof(buf));
	CHECK(n == 5 && buf[2] == 0x84, "again an RTS");
	g_now += 5000;						/* the IIgs stays silent */
	CHECK(gs_recv(buf, sizeof(buf)) == 0, "without a CTS the data is dropped");

	printf("6. A frame for another node\n");
	g_now += 10000;
	f[0] = 0x44; f[1] = 0x55; f[2] = 0x01;
	peer_send(f, 20);
	CHECK(gs_recv(buf, sizeof(buf)) == 0, "does not reach the IIgs");

	printf("%s\n", localtalk_status());
	localtalk_port_off(1);
	CHECK(localtalk_port() == -1, "AppleTalk is off again");
	printf(g_fail ? "%d FAILED\n" : "all AppleTalk tests passed\n", g_fail);
	return g_fail ? 1 : 0;
}
