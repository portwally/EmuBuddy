/* Uthernet II (WIZnet W5100) emulation for EmuBuddy. See uthernet2.h.
 *
 * W5100 memory map (32 KB address space):
 *   0x0000-0x002F  common registers (MR, gateway, subnet, MAC, IP, IR, ...)
 *   0x0400-0x07FF  socket registers, 0x100 per socket
 *   0x4000-0x5FFF  transmit buffer (8 KB, split between sockets by TMSR)
 *   0x6000-0x7FFF  receive buffer  (8 KB, split between sockets by RMSR)
 *
 * Supported: indirect bus mode with auto-increment, software reset, buffer
 * size registers, MACRAW on socket 0 (send/receive raw frames with the
 * optional MAC filter), and the hardware TCP and UDP sockets (all four):
 * they are this computer's own sockets (see "hardware sockets" below), so
 * a program that lets the chip do TCP/IP needs no IP stack of its own.
 * IPRAW is not emulated: opening it is logged. */
#include "uthernet2.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#ifdef _WIN32
# include <winsock2.h>
# include <ws2tcpip.h>
typedef SOCKET hsock;
# define HS_BAD		INVALID_SOCKET
# define hs_close(s)	closesocket(s)
# define HS_WOULDBLOCK	(WSAGetLastError() == WSAEWOULDBLOCK)
# define HS_INPROGRESS	(WSAGetLastError() == WSAEWOULDBLOCK)
#else
# include <sys/types.h>
# include <sys/socket.h>
# include <netinet/in.h>
# include <netinet/tcp.h>
# include <arpa/inet.h>
# include <poll.h>
# include <fcntl.h>
# include <unistd.h>
# include <errno.h>
typedef int hsock;
# define HS_BAD		(-1)
# define hs_close(s)	close(s)
# define HS_WOULDBLOCK	(errno == EAGAIN || errno == EWOULDBLOCK)
# define HS_INPROGRESS	(errno == EINPROGRESS)
#endif

#define MR_RST		0x80
#define MR_AI		0x02
#define MR_IND		0x01

/* common registers */
#define R_SHAR		0x0009		/* MAC address, 6 bytes */
#define R_IR		0x0015
#define R_RMSR		0x001A
#define R_TMSR		0x001B

/* socket register offsets */
#define S_MR		0x00
#define S_CR		0x01
#define S_IR		0x02
#define S_SR		0x03
#define S_PORT		0x04		/* source port */
#define S_DIPR		0x0C		/* destination IP */
#define S_DPORT		0x10		/* destination port */
#define S_TX_FSR	0x20
#define S_TX_RD		0x22
#define S_TX_WR		0x24
#define S_RX_RSR	0x26
#define S_RX_RD		0x28

/* Sn_MR protocols */
#define P_CLOSED	0
#define P_TCP		1
#define P_UDP		2
#define P_IPRAW		3
#define P_MACRAW	4
#define MR_MF		0x40		/* MAC filter (MACRAW) */

/* Sn_CR commands */
#define C_OPEN		0x01
#define C_LISTEN	0x02
#define C_CONNECT	0x04
#define C_DISCON	0x08
#define C_CLOSE		0x10
#define C_SEND		0x20
#define C_SEND_MAC	0x21
#define C_SEND_KEEP	0x22
#define C_RECV		0x40

/* Sn_SR status */
#define SR_CLOSED	0x00
#define SR_INIT		0x13
#define SR_LISTEN	0x14
#define SR_SYNSENT	0x15
#define SR_ESTABLISHED	0x17
#define SR_FIN_WAIT	0x18
#define SR_CLOSE_WAIT	0x1C
#define SR_UDP		0x22
#define SR_IPRAW	0x32
#define SR_MACRAW	0x42

/* Sn_IR bits */
#define IR_CON		0x01
#define IR_DISCON	0x02
#define IR_RECV		0x04
#define IR_TIMEOUT	0x08
#define IR_SEND_OK	0x10

#define TX_BASE		0x4000
#define RX_BASE		0x6000

static uint8_t	g_mem[0x8000];
static uint8_t	g_mode;
static uint16_t	g_addr;

typedef struct {
	uint16_t	tx_base, tx_size;	/* in g_mem, size 0 = none */
	uint16_t	rx_base, rx_size;
	uint16_t	rx_wr;			/* internal receive write pointer */
	uint16_t	rx_rsr;			/* received size not yet RECV'd */
} Sock;
static Sock	g_sock[4];

/* Frames from the network waiting for room in the receive buffer. */
#define QN	64
#define QMAX	1600
static uint8_t	g_q[QN][QMAX];
static int	g_qlen[QN];
static int	g_qhead, g_qcount;

static unsigned	g_tx_count, g_rx_count;
static int	g_net_up;

static void	hw_close(int s);
static void	hw_command(int s, int proto, int cmd);
static void	hw_poll(void);

static uint16_t
get16(int a)
{
	return (uint16_t)((g_mem[a] << 8) | g_mem[a + 1]);
}

static void
put16(int a, uint16_t v)
{
	g_mem[a] = (uint8_t)(v >> 8);
	g_mem[a + 1] = (uint8_t)v;
}

static int
sreg(int s, int off)
{
	return 0x0400 + s * 0x100 + off;
}

static void
compute_buffers(void)
{
	int	s, tx = 0, rx = 0, sz;

	for(s = 0; s < 4; s++) {
		sz = 1024 << ((g_mem[R_TMSR] >> (2 * s)) & 3);
		if(tx + sz > 0x2000) {
			sz = 0;
		}
		g_sock[s].tx_base = (uint16_t)(TX_BASE + tx);
		g_sock[s].tx_size = (uint16_t)sz;
		tx += sz;

		sz = 1024 << ((g_mem[R_RMSR] >> (2 * s)) & 3);
		if(rx + sz > 0x2000) {
			sz = 0;
		}
		g_sock[s].rx_base = (uint16_t)(RX_BASE + rx);
		g_sock[s].rx_size = (uint16_t)sz;
		rx += sz;
	}
}

void
uthernet2_reset(void)
{
	int	s;

	for(s = 0; s < 4; s++) {
		hw_close(s);		/* (the host sockets go with the chip) */
	}
	memset(g_mem, 0, sizeof(g_mem));
	memset(g_sock, 0, sizeof(g_sock));
	g_mode = 0;
	g_addr = 0;
	put16(0x0017, 0x07D0);		/* RTR: retry time 200ms */
	g_mem[0x0019] = 0x08;		/* RCR: retry count */
	g_mem[R_RMSR] = 0x55;		/* 2 KB per socket */
	g_mem[R_TMSR] = 0x55;
	for(s = 0; s < 4; s++) {
		g_mem[sreg(s, 0x16)] = 0x80;	/* TTL */
	}
	compute_buffers();
	g_qhead = g_qcount = 0;
}

static uint16_t
tx_fsr(int s)
{
	uint16_t used = (uint16_t)(get16(sreg(s, S_TX_WR)) -
						get16(sreg(s, S_TX_RD)));

	return used > g_sock[s].tx_size ? 0 :
					(uint16_t)(g_sock[s].tx_size - used);
}

/* EMUBUDDY_NETDEBUG=1 also prints a one-line summary of every frame. */
static void
pkt_log(const char *dir, const uint8_t *f, int len)
{
	static int	on = -1;
	int		type, proto;

	if(on < 0) {
		on = getenv("EMUBUDDY_NETDEBUG") != NULL;
	}
	if(!on || len < 14) {
		return;
	}
	type = (f[12] << 8) | f[13];
	if(type == 0x0806 && len >= 42) {
		printf("NET %s ARP %s %d.%d.%d.%d\n", dir, f[21] == 1 ? "who-has" :
			"reply", f[38], f[39], f[40], f[41]);
	} else if(type == 0x0800 && len >= 38) {
		proto = f[23];
		printf("NET %s IP %d.%d.%d.%d -> %d.%d.%d.%d %s port %d -> %d len %d\n",
			dir, f[26], f[27], f[28], f[29], f[30], f[31], f[32], f[33],
			proto == 6 ? "TCP" : proto == 17 ? "UDP" : "proto",
			(f[34] << 8) | f[35], (f[36] << 8) | f[37], len);
	} else {
		printf("NET %s type %04x len %d\n", dir, type, len);
	}
}

/* ---- commands ---- */

static void
sock_send_macraw(int s)
{
	Sock		*k = &g_sock[s];
	uint16_t	rd = get16(sreg(s, S_TX_RD));
	uint16_t	wr = get16(sreg(s, S_TX_WR));
	uint16_t	len = (uint16_t)(wr - rd), i, mask;
	uint8_t		frame[QMAX];

	if(k->tx_size == 0) {
		return;
	}
	mask = (uint16_t)(k->tx_size - 1);
	if(len > k->tx_size || len > sizeof(frame)) {
		printf("Uthernet II: bad send length %u\n", len);
		len = 0;
	}
	for(i = 0; i < len; i++) {
		frame[i] = g_mem[k->tx_base + ((rd + i) & mask)];
	}
	if(len >= 14 && g_net_up) {
		pkt_log("out", frame, len);
		net_send(frame, len);
		g_tx_count++;
	}
	put16(sreg(s, S_TX_RD), wr);
	g_mem[sreg(s, S_IR)] |= IR_SEND_OK;
}

static void
sock_command(int s, int cmd)
{
	int	proto = g_mem[sreg(s, S_MR)] & 0x0f;
	Sock	*k = &g_sock[s];

	if(cmd == C_OPEN) {
		hw_close(s);
		put16(sreg(s, S_TX_RD), get16(sreg(s, S_TX_WR)));
		k->rx_wr = get16(sreg(s, S_RX_RD));
		k->rx_rsr = 0;
	}
	if(cmd == C_RECV) {
		k->rx_rsr = (uint16_t)(k->rx_wr - get16(sreg(s, S_RX_RD)));
		if(k->rx_rsr > k->rx_size) {
			k->rx_rsr = 0;		/* guest moved RX_RD wrongly */
			k->rx_wr = get16(sreg(s, S_RX_RD));
		}
		return;
	}
	if(proto == P_TCP || proto == P_UDP) {
		hw_command(s, proto, cmd);
		return;
	}
	switch(cmd) {
	case C_OPEN:
		switch(proto) {
		case P_MACRAW:
			if(s != 0) {
				printf("Uthernet II: MACRAW only on socket 0\n");
				g_mem[sreg(s, S_SR)] = SR_CLOSED;
				break;
			}
			g_mem[sreg(s, S_SR)] = SR_MACRAW;
			g_qhead = g_qcount = 0;
			if(!g_net_up) {
				g_net_up = (net_init() == 0);
			}
			printf("Uthernet II: socket 0 opened in MACRAW mode "
				"(network: %s)\n", g_net_up ? net_backend_name() :
							"unavailable");
			break;
		case P_IPRAW:
			printf("Uthernet II: socket %d opened in IPRAW mode - "
				"not emulated\n", s);
			g_mem[sreg(s, S_SR)] = SR_IPRAW;
			break;
		default:
			g_mem[sreg(s, S_SR)] = SR_CLOSED;
		}
		break;
	case C_CLOSE:
	case C_DISCON:
		g_mem[sreg(s, S_SR)] = SR_CLOSED;
		if(s == 0) {
			g_qhead = g_qcount = 0;
		}
		break;
	case C_SEND:
	case C_SEND_MAC:
		if(g_mem[sreg(s, S_SR)] == SR_MACRAW) {
			sock_send_macraw(s);
		} else {
			/* not emulated: pretend it went out */
			put16(sreg(s, S_TX_RD), get16(sreg(s, S_TX_WR)));
			g_mem[sreg(s, S_IR)] |= IR_SEND_OK;
		}
		break;
	default:
		break;
	}
}

/* ---- memory access ---- */

static int
mem_read(uint16_t a)
{
	int	s, off;

	if(a == 0) {
		return g_mode;
	}
	if(a >= 0x0400 && a < 0x0800) {
		s = (a >> 8) - 4;
		off = a & 0xff;
		switch(off) {
		case S_CR:
			return 0;		/* commands complete at once */
		case S_TX_FSR:
			return tx_fsr(s) >> 8;
		case S_TX_FSR + 1:
			return tx_fsr(s) & 0xff;
		case S_RX_RSR:
			return g_sock[s].rx_rsr >> 8;
		case S_RX_RSR + 1:
			return g_sock[s].rx_rsr & 0xff;
		}
		return g_mem[a];
	}
	if(a < 0x0030 || a >= 0x4000) {
		return g_mem[a & 0x7fff];
	}
	return 0;
}

static void
mem_write(uint16_t a, int val)
{
	int	s, off;

	val &= 0xff;
	if(a == 0) {
		if(val & MR_RST) {
			uthernet2_reset();
		} else {
			g_mode = (uint8_t)val;
		}
		return;
	}
	if(a < 0x0030) {
		if(a == R_IR) {
			g_mem[a] &= (uint8_t)~val;	/* write 1 to clear */
			return;
		}
		g_mem[a] = (uint8_t)val;
		if(a == R_RMSR || a == R_TMSR) {
			compute_buffers();
		}
		return;
	}
	if(a >= 0x0400 && a < 0x0800) {
		s = (a >> 8) - 4;
		off = a & 0xff;
		switch(off) {
		case S_CR:
			sock_command(s, val);
			return;
		case S_IR:
			g_mem[a] &= (uint8_t)~val;
			if(!g_mem[a]) {
				g_mem[R_IR] &= (uint8_t)~(1 << s);
			}
			return;
		case S_SR:
		case S_TX_FSR: case S_TX_FSR + 1:
		case S_TX_RD: case S_TX_RD + 1:
		case S_RX_RSR: case S_RX_RSR + 1:
			return;			/* read-only */
		}
		g_mem[a] = (uint8_t)val;
		return;
	}
	if(a >= 0x4000 && a < 0x8000) {
		g_mem[a] = (uint8_t)val;
	}
}

static void
addr_step(void)
{
	if(!(g_mode & MR_AI)) {
		return;
	}
	g_addr++;
	/* auto-increment wraps inside the TX and RX buffer regions */
	if(g_addr == 0x6000) {
		g_addr = 0x4000;
	} else if(g_addr == 0x8000) {
		g_addr = 0x6000;
	}
}

/* EMUBUDDY_NETDEBUG=1 logs the first register accesses (driver bring-up). */
static void
dbg(const char *rw, int reg, int val)
{
	static int	n = -1;

	if(n < 0) {
		n = getenv("EMUBUDDY_NETDEBUG") ? 0 : 1000000;
	}
	if(n < 400) {
		n++;
		printf("U2 %s reg%x addr=%04x val=%02x\n", rw, reg, g_addr, val);
	}
}

int
uthernet2_read(int reg)
{
	int	v;

	switch(reg & 0x0f) {
	case 4:
		return g_mode;
	case 5:
		return g_addr >> 8;
	case 6:
		return g_addr & 0xff;
	case 7:
		v = mem_read(g_addr);
		dbg("rd", reg, v);
		addr_step();
		return v;
	}
	dbg("rd", reg, 0);
	return 0;
}

void
uthernet2_write(int reg, int val)
{
	dbg("wr", reg, val);
	switch(reg & 0x0f) {
	case 4:
		mem_write(0, val);
		break;
	case 5:
		g_addr = (uint16_t)(((val & 0x7f) << 8) | (g_addr & 0xff));
		break;
	case 6:
		g_addr = (uint16_t)((g_addr & 0x7f00) | (val & 0xff));
		break;
	case 7:
		mem_write(g_addr, val);
		addr_step();
		break;
	}
}

/* ---- receive path ---- */

int
uthernet2_active(void)
{
	return g_mem[sreg(0, S_SR)] == SR_MACRAW;
}

void
uthernet2_rx_frame(const uint8_t *frame, int len)
{
	int	slot;

	if(!uthernet2_active() || len < 14 || len > QMAX) {
		return;
	}
	if(g_mem[sreg(0, S_MR)] & MR_MF) {
		/* MAC filter: own address, broadcast or multicast only */
		if(!(frame[0] & 1) && memcmp(frame, &g_mem[R_SHAR], 6) != 0) {
			return;
		}
	}
	if(g_qcount == QN) {
		return;				/* queue full: drop */
	}
	slot = (g_qhead + g_qcount) % QN;
	memcpy(g_q[slot], frame, (size_t)len);
	g_qlen[slot] = len;
	g_qcount++;
}

static void
deliver(void)
{
	Sock		*k = &g_sock[0];
	uint16_t	mask, need, free_space;
	int		i, len;
	uint8_t		*f;

	if(!uthernet2_active() || k->rx_size == 0) {
		return;
	}
	mask = (uint16_t)(k->rx_size - 1);
	while(g_qcount > 0) {
		len = g_qlen[g_qhead];
		f = g_q[g_qhead];
		need = (uint16_t)(len + 2);
		free_space = (uint16_t)(k->rx_size - k->rx_rsr);
		if(need > free_space) {
			break;			/* wait for the guest to read */
		}
		g_mem[k->rx_base + (k->rx_wr & mask)] = (uint8_t)(need >> 8);
		g_mem[k->rx_base + ((k->rx_wr + 1) & mask)] = (uint8_t)need;
		for(i = 0; i < len; i++) {
			g_mem[k->rx_base + ((k->rx_wr + 2 + i) & mask)] = f[i];
		}
		pkt_log("in ", f, len);
		k->rx_wr = (uint16_t)(k->rx_wr + need);
		k->rx_rsr = (uint16_t)(k->rx_rsr + need);
		g_mem[sreg(0, S_IR)] |= IR_RECV;
		g_mem[R_IR] |= 1;
		g_rx_count++;
		g_qhead = (g_qhead + 1) % QN;
		g_qcount--;
	}
}

void
uthernet2_poll(void)
{
	if(g_net_up) {
		net_poll();
	}
	deliver();
	hw_poll();
}

void
uthernet2_counters(unsigned *tx, unsigned *rx)
{
	*tx = g_tx_count;
	*rx = g_rx_count;
}

/* ---- hardware sockets ----
 * Sockets opened in TCP or UDP mode are this computer's own sockets: the
 * chip's TCP/IP is the host's. As with slirp (net_slirp.c), the address
 * NET_HOST_ADDR (192.168.64.254) means this computer, 127.0.0.1; every
 * other address is reached as this computer reaches it.
 *   TCP: CONNECT (non-blocking: SYNSENT, then ESTABLISHED and Sn_IR CON,
 *        or CLOSED and TIMEOUT), LISTEN (on 127.0.0.1 only, so only
 *        programs on this computer - another EmuBuddy, say - can come in:
 *        then ESTABLISHED and CON, Sn_DIPR/Sn_DPORT the caller), SEND, RECV,
 *        DISCON (FIN_WAIT, CLOSED when the other side closes too), CLOSE;
 *        the other side closing: CLOSE_WAIT and DISCON.
 *   UDP: OPEN binds Sn_PORT (any port when 0) on every interface; SEND to
 *        Sn_DIPR:Sn_DPORT; each datagram received is put in the receive
 *        buffer after an 8-byte header (IP, port, length), as the chip does.
 * SEND takes the bytes at once (TX_RD moves to TX_WR) and sets SEND_OK
 * once the host has taken them all. Everything is polled once a frame
 * (uthernet2_poll), so nothing blocks the emulator. */

#define HW_OUTMAX	(256 * 1024)	/* bytes waiting to go out, at most */
#define HW_CONN_SECS	20		/* CONNECT gives up after this */
#define HW_FIN_SECS	5		/* DISCON: CLOSED by then, whatever */

typedef struct {
	hsock	fd;			/* the connection or UDP socket */
	hsock	lfd;			/* LISTEN: the listening socket */
	int	open;			/* fd, lfd valid */
	time_t	since;			/* CONNECT or DISCON started */
	uint8_t	*out;			/* SEND bytes the host hasn't taken */
	int	outlen;
} Hw;
static Hw	g_hw[4];
static int	g_hw_init;

static void
hw_startup(void)
{
	int	s;

	if(g_hw_init) {
		return;
	}
#ifdef _WIN32
	{
		WSADATA	wsa;

		WSAStartup(MAKEWORD(2, 2), &wsa);
	}
#endif
	for(s = 0; s < 4; s++) {
		g_hw[s].fd = g_hw[s].lfd = HS_BAD;
	}
	g_hw_init = 1;
}

static void
hw_nonblock(hsock fd)
{
#ifdef _WIN32
	u_long	nb = 1;

	ioctlsocket(fd, FIONBIO, &nb);
#else
	int	one = 1;

	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
# ifdef SO_NOSIGPIPE
	setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, (const char *)&one, sizeof(one));
# else
	(void)one;
# endif
#endif
}

static int
hw_send(hsock fd, const uint8_t *p, int n)
{
#ifdef MSG_NOSIGNAL
	return (int)send(fd, (const char *)p, (size_t)n, MSG_NOSIGNAL);
#else
	return (int)send(fd, (const char *)p, (size_t)n, 0);
#endif
}

static void
hw_close(int s)
{
	Hw	*h = &g_hw[s];

	if(!g_hw_init) {
		return;
	}
	if(h->fd != HS_BAD) {
		hs_close(h->fd);
	}
	if(h->lfd != HS_BAD) {
		hs_close(h->lfd);
	}
	h->fd = h->lfd = HS_BAD;
	h->open = 0;
	free(h->out);
	h->out = NULL;
	h->outlen = 0;
}

static void
set_ir(int s, int bits)
{
	g_mem[sreg(s, S_IR)] |= (uint8_t)bits;
	g_mem[R_IR] |= (uint8_t)(1 << s);
}

/* Sn_DIPR:Sn_DPORT as the host's address (NET_HOST_ADDR: 127.0.0.1) */
static void
hw_dest(int s, struct sockaddr_in *sa)
{
	const uint8_t	*ip = &g_mem[sreg(s, S_DIPR)];
	char		a[16];

	memset(sa, 0, sizeof(*sa));
	sa->sin_family = AF_INET;
	sa->sin_port = htons(get16(sreg(s, S_DPORT)));
	snprintf(a, sizeof(a), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
	if(strcmp(a, NET_HOST_ADDR) == 0) {
		strcpy(a, "127.0.0.1");
	}
	inet_pton(AF_INET, a, &sa->sin_addr);
}

/* the other way: a host address into 4 bytes (127.0.0.1: NET_HOST_ADDR) */
static void
hw_ipbytes(const struct sockaddr_in *sa, uint8_t *ip)
{
	uint32_t	a = ntohl(sa->sin_addr.s_addr);

	if(a == 0x7F000001) {
		inet_pton(AF_INET, NET_HOST_ADDR, ip);
		return;
	}
	ip[0] = (uint8_t)(a >> 24);
	ip[1] = (uint8_t)(a >> 16);
	ip[2] = (uint8_t)(a >> 8);
	ip[3] = (uint8_t)a;
}

/* the bytes TX_RD..TX_WR, taken: TX_RD = TX_WR; their number */
static int
hw_take(int s, uint8_t *buf, int max)
{
	Sock		*k = &g_sock[s];
	uint16_t	rd = get16(sreg(s, S_TX_RD));
	uint16_t	wr = get16(sreg(s, S_TX_WR));
	uint16_t	len = (uint16_t)(wr - rd), mask, i;

	if(k->tx_size == 0 || len > k->tx_size || len > max) {
		put16(sreg(s, S_TX_RD), wr);
		return 0;
	}
	mask = (uint16_t)(k->tx_size - 1);
	for(i = 0; i < len; i++) {
		buf[i] = g_mem[k->tx_base + ((rd + i) & mask)];
	}
	put16(sreg(s, S_TX_RD), wr);
	return len;
}

/* n bytes into the receive buffer (the room checked by the caller) */
static void
hw_put(int s, const uint8_t *p, int n)
{
	Sock		*k = &g_sock[s];
	uint16_t	mask = (uint16_t)(k->rx_size - 1);
	int		i;

	for(i = 0; i < n; i++) {
		g_mem[k->rx_base + ((k->rx_wr + i) & mask)] = p[i];
	}
	k->rx_wr = (uint16_t)(k->rx_wr + n);
	k->rx_rsr = (uint16_t)(k->rx_rsr + n);
}

static void
hw_command(int s, int proto, int cmd)
{
	Hw			*h = &g_hw[s];
	int			sr = g_mem[sreg(s, S_SR)], one = 1, n;
	struct sockaddr_in	sa;
	uint8_t			buf[0x2000];

	hw_startup();
	switch(cmd) {
	case C_OPEN:
		if(proto == P_TCP) {
			g_mem[sreg(s, S_SR)] = SR_INIT;
			break;
		}
		h->fd = socket(AF_INET, SOCK_DGRAM, 0);
		if(h->fd == HS_BAD) {
			g_mem[sreg(s, S_SR)] = SR_CLOSED;
			break;
		}
		hw_nonblock(h->fd);
		memset(&sa, 0, sizeof(sa));
		sa.sin_family = AF_INET;
		sa.sin_port = htons(get16(sreg(s, S_PORT)));
		if(bind(h->fd, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
			printf("Uthernet II: socket %d: UDP port %d is taken, "
				"any port instead\n", s, get16(sreg(s, S_PORT)));
			sa.sin_port = 0;
			bind(h->fd, (struct sockaddr *)&sa, sizeof(sa));
		}
		h->open = 1;
		g_mem[sreg(s, S_SR)] = SR_UDP;
		break;
	case C_CONNECT:
		if(sr != SR_INIT) {
			break;
		}
		hw_dest(s, &sa);
		h->fd = socket(AF_INET, SOCK_STREAM, 0);
		if(h->fd == HS_BAD) {
			g_mem[sreg(s, S_SR)] = SR_CLOSED;
			set_ir(s, IR_TIMEOUT);
			break;
		}
		setsockopt(h->fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&one,
								sizeof(one));
		hw_nonblock(h->fd);
		h->open = 1;
		h->since = time(NULL);
		if(connect(h->fd, (struct sockaddr *)&sa, sizeof(sa)) == 0) {
			g_mem[sreg(s, S_SR)] = SR_ESTABLISHED;
			set_ir(s, IR_CON);
		} else if(HS_INPROGRESS) {
			g_mem[sreg(s, S_SR)] = SR_SYNSENT;
		} else {
			hw_close(s);
			g_mem[sreg(s, S_SR)] = SR_CLOSED;
			set_ir(s, IR_TIMEOUT);
		}
		break;
	case C_LISTEN:
		if(sr != SR_INIT) {
			break;
		}
		h->lfd = socket(AF_INET, SOCK_STREAM, 0);
		if(h->lfd == HS_BAD) {
			g_mem[sreg(s, S_SR)] = SR_CLOSED;
			break;
		}
		setsockopt(h->lfd, SOL_SOCKET, SO_REUSEADDR, (const char *)&one,
								sizeof(one));
		hw_nonblock(h->lfd);
		memset(&sa, 0, sizeof(sa));
		sa.sin_family = AF_INET;
		sa.sin_port = htons(get16(sreg(s, S_PORT)));
		sa.sin_addr.s_addr = htonl(0x7F000001);	/* this computer only */
		if(bind(h->lfd, (struct sockaddr *)&sa, sizeof(sa)) != 0 ||
						listen(h->lfd, 1) != 0) {
			printf("Uthernet II: socket %d: cannot listen on port %d\n",
						s, get16(sreg(s, S_PORT)));
			hw_close(s);
			g_mem[sreg(s, S_SR)] = SR_CLOSED;
			break;
		}
		h->open = 1;
		g_mem[sreg(s, S_SR)] = SR_LISTEN;
		break;
	case C_SEND:
	case C_SEND_MAC:
		n = hw_take(s, buf, sizeof(buf));
		if(sr == SR_UDP && h->fd != HS_BAD) {
			hw_dest(s, &sa);
			if(n > 0) {
				sendto(h->fd, (const char *)buf, (size_t)n, 0,
					(struct sockaddr *)&sa, sizeof(sa));
				g_tx_count++;
			}
			set_ir(s, IR_SEND_OK);
		} else if((sr == SR_ESTABLISHED || sr == SR_CLOSE_WAIT) &&
							h->fd != HS_BAD) {
			if(h->outlen + n > HW_OUTMAX) {
				n = HW_OUTMAX - h->outlen;	/* (too much: lost) */
			}
			if(n > 0) {
				h->out = realloc(h->out, (size_t)(h->outlen + n));
				memcpy(h->out + h->outlen, buf, (size_t)n);
				h->outlen += n;
				g_tx_count++;
			}
			if(h->outlen == 0) {
				set_ir(s, IR_SEND_OK);	/* (nothing to send) */
			}
			hw_poll();		/* (most often it goes at once) */
		} else {
			set_ir(s, IR_SEND_OK);
		}
		break;
	case C_SEND_KEEP:
		break;
	case C_DISCON:
		if(sr == SR_ESTABLISHED && h->fd != HS_BAD) {
#ifdef _WIN32
			shutdown(h->fd, SD_SEND);
#else
			shutdown(h->fd, SHUT_WR);
#endif
			h->since = time(NULL);
			g_mem[sreg(s, S_SR)] = SR_FIN_WAIT;
			break;
		}
		/* fall through: nothing to wait for */
	case C_CLOSE:
		hw_close(s);
		g_mem[sreg(s, S_SR)] = SR_CLOSED;
		if(cmd == C_DISCON) {
			set_ir(s, IR_DISCON);
		}
		break;
	}
}

/* once a frame: connections made or refused, callers, bytes out and in */
static void
hw_poll(void)
{
	int			s, sr, n, room, err;
	socklen_t		len;
	struct sockaddr_in	sa;
	uint8_t			buf[0x2000];
	hsock			c;
	Hw			*h;
	Sock			*k;

	if(!g_hw_init) {
		return;
	}
	for(s = 0; s < 4; s++) {
		h = &g_hw[s];
		k = &g_sock[s];
		if(!h->open) {
			continue;
		}
		sr = g_mem[sreg(s, S_SR)];
		if(sr == SR_SYNSENT) {
#ifdef _WIN32
			fd_set		w, e;
			struct timeval	tv = { 0, 0 };

			FD_ZERO(&w); FD_ZERO(&e);
			FD_SET(h->fd, &w); FD_SET(h->fd, &e);
			n = select(0, NULL, &w, &e, &tv);
			if(n > 0 && FD_ISSET(h->fd, &e)) {
				n = -1;
			}
#else
			struct pollfd	pf = { h->fd, POLLOUT, 0 };

			n = poll(&pf, 1, 0);
#endif
			if(n > 0) {
				err = 0;
				len = sizeof(err);
				getsockopt(h->fd, SOL_SOCKET, SO_ERROR, (char *)&err,
									&len);
				if(err == 0) {
					g_mem[sreg(s, S_SR)] = SR_ESTABLISHED;
					set_ir(s, IR_CON);
					continue;
				}
			}
			if(n < 0 || (n > 0) ||
				time(NULL) - h->since > HW_CONN_SECS) {
				hw_close(s);
				g_mem[sreg(s, S_SR)] = SR_CLOSED;
				set_ir(s, IR_TIMEOUT);
			}
			continue;
		}
		if(sr == SR_LISTEN) {
			len = sizeof(sa);
			c = accept(h->lfd, (struct sockaddr *)&sa, &len);
			if(c == HS_BAD) {
				continue;
			}
			hs_close(h->lfd);
			h->lfd = HS_BAD;
			h->fd = c;
			hw_nonblock(c);
			n = 1;
			setsockopt(c, IPPROTO_TCP, TCP_NODELAY, (const char *)&n,
								sizeof(n));
			hw_ipbytes(&sa, &g_mem[sreg(s, S_DIPR)]);
			put16(sreg(s, S_DPORT), ntohs(sa.sin_port));
			g_mem[sreg(s, S_SR)] = SR_ESTABLISHED;
			set_ir(s, IR_CON);
			continue;
		}
		if(sr == SR_UDP) {
			room = k->rx_size - k->rx_rsr - 8;
			while(room > 0) {
				len = sizeof(sa);
				n = (int)recvfrom(h->fd, (char *)buf, sizeof(buf),
					MSG_PEEK, (struct sockaddr *)&sa, &len);
				if(n < 0 || n > room) {
					break;	/* none, or no room yet */
				}
				len = sizeof(sa);
				n = (int)recvfrom(h->fd, (char *)buf, sizeof(buf), 0,
						(struct sockaddr *)&sa, &len);
				if(n < 0) {
					break;
				}
				{
					uint8_t	hd[8];

					hw_ipbytes(&sa, hd);
					hd[4] = (uint8_t)(ntohs(sa.sin_port) >> 8);
					hd[5] = (uint8_t)ntohs(sa.sin_port);
					hd[6] = (uint8_t)(n >> 8);
					hd[7] = (uint8_t)n;
					hw_put(s, hd, 8);
				}
				hw_put(s, buf, n);
				set_ir(s, IR_RECV);
				g_rx_count++;
				room = k->rx_size - k->rx_rsr - 8;
			}
			continue;
		}
		if(sr != SR_ESTABLISHED && sr != SR_CLOSE_WAIT &&
							sr != SR_FIN_WAIT) {
			continue;
		}
		/* out */
		if(h->outlen > 0) {
			n = hw_send(h->fd, h->out, h->outlen);
			if(n > 0) {
				memmove(h->out, h->out + n, (size_t)(h->outlen - n));
				h->outlen -= n;
			} else if(n < 0 && !HS_WOULDBLOCK) {
				h->outlen = 0;
			}
			if(h->outlen == 0) {
				set_ir(s, IR_SEND_OK);
			}
		}
		/* in */
		room = k->rx_size - k->rx_rsr;
		if(sr != SR_CLOSE_WAIT && room > 0) {
			n = (int)recv(h->fd, (char *)buf,
				(size_t)(room < (int)sizeof(buf) ? room :
							(int)sizeof(buf)), 0);
			if(n > 0) {
				hw_put(s, buf, n);
				set_ir(s, IR_RECV);
				g_rx_count++;
			} else if(n == 0 || !HS_WOULDBLOCK) {
				/* the other side closed (or the line broke) */
				if(sr == SR_FIN_WAIT || n < 0) {
					hw_close(s);
					g_mem[sreg(s, S_SR)] = SR_CLOSED;
				} else {
					g_mem[sreg(s, S_SR)] = SR_CLOSE_WAIT;
				}
				set_ir(s, IR_DISCON);
			}
		}
		if(sr == SR_FIN_WAIT && h->open &&
				time(NULL) - h->since > HW_FIN_SECS) {
			hw_close(s);
			g_mem[sreg(s, S_SR)] = SR_CLOSED;
		}
	}
}
