/* FujiNet for EmuBuddy: the connection to FujiNet-PC.
 *
 * FujiNet-PC is the FujiNet firmware as a program on this computer. It
 * connects to the emulator by TCP (port 1985) and answers SmartPort calls
 * that come as SLIP frames: "SmartPort over SLIP", as the AppleWin fork of
 * the FujiNet project does it. The format, read from fujinet-firmware
 * (lib/devrelay) and checked against a running FujiNet-PC:
 *
 *   frame:    C0, the bytes, C0; inside, C0 is sent as DB DC and DB as DB DD
 *   request:  sequence number, command, parameter count, unit, 0, 0,
 *             5 bytes that depend on the command, then data
 *   answer:   the sequence number again, the SmartPort error (0 = fine),
 *             then data
 *
 * There is no greeting. The emulator sends INIT to unit 1, 2, 3 ... until an
 * answer is not 0: that unit is the last. FujiNet-PC answers a second round
 * of INITs not at all, so the units are counted once for each connection.
 *
 * EmuBuddy listens on this computer only (127.0.0.1) and takes one
 * connection. A call waits for its answer; the IIgs stands still meanwhile. */

#include "fujinet.h"
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
# include <winsock2.h>
# include <ws2tcpip.h>
typedef SOCKET fn_sock;
# define FN_BAD		INVALID_SOCKET
# define fn_close(s)	closesocket(s)
#else
# include <sys/types.h>
# include <sys/socket.h>
# include <sys/select.h>
# include <sys/time.h>
# include <netinet/in.h>
# include <netinet/tcp.h>
# include <arpa/inet.h>
# include <unistd.h>
# include <errno.h>
typedef int fn_sock;
# define FN_BAD		(-1)
# define fn_close(s)	close(s)
#endif

#define SLIP_END	0xc0
#define SLIP_ESC	0xdb
#define SLIP_ESC_END	0xdc
#define SLIP_ESC_ESC	0xdd

#define MAX_UNITS	32
#define FRAME_MAX	(FUJINET_MAX_DATA + 64)

static fn_sock	g_listen = FN_BAD;
static fn_sock	g_conn = FN_BAD;
static int	g_units = 0;
static int	g_seq = 0;
static int	g_timeout_ms = 10000;
static unsigned char g_rx[2 * FRAME_MAX + 16];	/* bytes read, not yet used */
static int	g_rx_len = 0;

void
fujinet_set_timeout_ms(int ms)
{
	g_timeout_ms = ms;
}

int
fujinet_units(void)
{
	return (g_conn != FN_BAD) ? g_units : 0;
}

static void
conn_drop(const char *why)
{
	if(g_conn != FN_BAD) {
		printf("fujinet: connection closed (%s)\n", why);
		fn_close(g_conn);
	}
	g_conn = FN_BAD;
	g_units = 0;
	g_rx_len = 0;
}

/* Waits up to ms for s to have something to read. 1 yes, 0 no. */
static int
wait_readable(fn_sock s, int ms)
{
	fd_set	set;
	struct timeval tv;

	FD_ZERO(&set);
	FD_SET(s, &set);
	tv.tv_sec = ms / 1000;
	tv.tv_usec = (ms % 1000) * 1000;
	return select((int)s + 1, &set, 0, 0, &tv) > 0;
}

static int
send_all(const unsigned char *p, int n)
{
	int	r;

	while(n > 0) {
#ifdef MSG_NOSIGNAL
		r = (int)send(g_conn, (const char *)p, (size_t)n, MSG_NOSIGNAL);
#else
		r = (int)send(g_conn, (const char *)p, (size_t)n, 0);
#endif
		if(r <= 0) {
			return -1;
		}
		p += r;
		n -= r;
	}
	return 0;
}

/* Sends buf as one SLIP frame. 0 ok. */
static int
send_frame(const unsigned char *buf, int len)
{
	static unsigned char out[2 * FRAME_MAX + 2];
	int	i, n = 0;

	out[n++] = SLIP_END;
	for(i = 0; i < len; i++) {
		if(buf[i] == SLIP_END) {
			out[n++] = SLIP_ESC;
			out[n++] = SLIP_ESC_END;
		} else if(buf[i] == SLIP_ESC) {
			out[n++] = SLIP_ESC;
			out[n++] = SLIP_ESC_ESC;
		} else {
			out[n++] = buf[i];
		}
	}
	out[n++] = SLIP_END;
	return send_all(out, n);
}

/* Takes the next whole frame out of g_rx. Returns its length (it may be
 * longer than max: then it is cut), or -1 if there is no whole frame yet. */
static int
take_frame(unsigned char *buf, int max)
{
	int	start, end, i, n = 0;

	for(start = 0; start < g_rx_len && g_rx[start] != SLIP_END; start++) {
	}
	/* END bytes one after the other are empty frames: skip them */
	while(start + 1 < g_rx_len && g_rx[start + 1] == SLIP_END) {
		start++;
	}
	for(end = start + 1; end < g_rx_len && g_rx[end] != SLIP_END; end++) {
	}
	if(start >= g_rx_len || end >= g_rx_len) {
		if(start > 0) {			/* drop what is before the frame */
			memmove(g_rx, g_rx + start, (size_t)(g_rx_len - start));
			g_rx_len -= start;
		}
		return -1;
	}
	for(i = start + 1; i < end; i++) {
		unsigned char c = g_rx[i];

		if(c == SLIP_ESC && i + 1 < end) {
			c = (g_rx[++i] == SLIP_ESC_END) ? SLIP_END : SLIP_ESC;
		}
		if(n < max) {
			buf[n] = c;
		}
		n++;
	}
	/* the END that closes a frame may open the next one: keep it */
	memmove(g_rx, g_rx + end, (size_t)(g_rx_len - end));
	g_rx_len -= end;
	return n;
}

/* Waits up to ms for a frame. Its length, -1 no frame in time, -2 the
 * connection is gone. */
static int
recv_frame(unsigned char *buf, int max, int ms)
{
	int	n, r, waited = 0, step;

	for(;;) {
		n = take_frame(buf, max);
		if(n >= 0) {
			return n;
		}
		if(g_rx_len >= (int)sizeof(g_rx)) {
			g_rx_len = 0;		/* no frame this long: rubbish */
		}
		if(waited >= ms) {
			return -1;
		}
		step = (ms - waited > 200) ? 200 : ms - waited;
		waited += step;
		if(!wait_readable(g_conn, step)) {
			continue;
		}
		r = (int)recv(g_conn, (char *)g_rx + g_rx_len,
					sizeof(g_rx) - (size_t)g_rx_len, 0);
		if(r <= 0) {
			return -2;
		}
		g_rx_len += r;
	}
}

static int
call_ms(int cmd, int unit, const unsigned char *params, int pcount,
		const unsigned char *out, int out_len,
		unsigned char *in, int in_max, int *in_len, int ms)
{
	static unsigned char req[FRAME_MAX], ans[FRAME_MAX];
	int	n;

	if(in_len) {
		*in_len = 0;
	}
	if(g_conn == FN_BAD) {
		return FUJINET_ERR_NODEV;
	}
	if(out_len < 0 || out_len > FUJINET_MAX_DATA) {
		return FUJINET_ERR_IO;
	}
	g_seq = (g_seq + 1) & 0xff;
	memset(req, 0, 11);
	req[0] = (unsigned char)g_seq;
	req[1] = (unsigned char)cmd;
	req[2] = (unsigned char)pcount;
	req[3] = (unsigned char)unit;
	if(params) {
		memcpy(req + 6, params, 5);
	}
	if(out_len) {
		memcpy(req + 11, out, (size_t)out_len);
	}
	if(send_frame(req, 11 + out_len) != 0) {
		conn_drop("send failed");
		return FUJINET_ERR_NODEV;
	}
	for(;;) {
		n = recv_frame(ans, sizeof(ans), ms);
		if(n == -2) {
			conn_drop("FujiNet-PC went away");
			return FUJINET_ERR_NODEV;
		}
		if(n < 0) {
			return FUJINET_ERR_IO;		/* no answer in time */
		}
		if(n < 2 || n > (int)sizeof(ans)) {
			continue;			/* not an answer */
		}
		if(ans[0] == (unsigned char)g_seq) {
			break;
		}
		/* an answer to an older call that was given up: drop it */
	}
	/* an answer with an error may still carry data: the error counts */
	if(ans[1] == 0 && in && in_len) {
		*in_len = (n - 2 < in_max) ? n - 2 : in_max;
		memcpy(in, ans + 2, (size_t)*in_len);
	}
	return ans[1];
}

int
fujinet_call(int cmd, int unit, const unsigned char *params, int pcount,
		const unsigned char *out, int out_len,
		unsigned char *in, int in_max, int *in_len)
{
	if(unit < 1 || unit > fujinet_units()) {
		if(in_len) {
			*in_len = 0;
		}
		return FUJINET_ERR_NODEV;
	}
	return call_ms(cmd, unit, params, pcount, out, out_len, in, in_max,
							in_len, g_timeout_ms);
}

/* A new connection: counts its units with INIT */
static void
count_units(void)
{
	int	unit, err;

	g_units = 0;
	g_seq = 0;
	for(unit = 1; unit <= MAX_UNITS; unit++) {
		err = call_ms(FUJINET_INIT, unit, 0, 1, 0, 0, 0, 0, 0, 2000);
		if(g_conn == FN_BAD || err == FUJINET_ERR_IO) {
			break;			/* gone, or no answer */
		}
		g_units = unit;
		if(err != 0) {
			break;			/* the end of the chain */
		}
	}
	if(g_conn != FN_BAD) {
		printf("fujinet: FujiNet-PC connected, %d units\n", g_units);
	}
}

int
fujinet_start(int port)
{
	struct sockaddr_in addr;
	int	one = 1;
#ifdef _WIN32
	static int wsa_done;
	WSADATA	wsa;

	if(!wsa_done) {
		WSAStartup(MAKEWORD(2, 2), &wsa);
		wsa_done = 1;
	}
#endif
	fujinet_stop();
	g_listen = socket(AF_INET, SOCK_STREAM, 0);
	if(g_listen == FN_BAD) {
		return -1;
	}
	setsockopt(g_listen, SOL_SOCKET, SO_REUSEADDR, (const char *)&one,
								sizeof(one));
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons((unsigned short)port);
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);	/* this computer only */
	if(bind(g_listen, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
						listen(g_listen, 1) != 0) {
		printf("fujinet: cannot listen on 127.0.0.1:%d\n", port);
		fn_close(g_listen);
		g_listen = FN_BAD;
		return -1;
	}
	printf("fujinet: waiting for FujiNet-PC on 127.0.0.1:%d\n", port);
	return 0;
}

void
fujinet_stop(void)
{
	conn_drop("stopped");
	if(g_listen != FN_BAD) {
		fn_close(g_listen);
	}
	g_listen = FN_BAD;
}

void
fujinet_poll(void)
{
	unsigned char c;
	int	one = 1, r;

	if(g_listen == FN_BAD) {
		return;
	}
	if(g_conn != FN_BAD) {
		/* nothing is expected between calls: readable means closed,
		 * or bytes nobody asked for */
		if(wait_readable(g_conn, 0)) {
			r = (int)recv(g_conn, (char *)&c, 1, MSG_PEEK);
			if(r <= 0) {
				conn_drop("FujiNet-PC went away");
			} else {
				r = (int)recv(g_conn, (char *)g_rx,
							sizeof(g_rx), 0);
				g_rx_len = 0;
			}
		}
		return;
	}
	if(!wait_readable(g_listen, 0)) {
		return;
	}
	g_conn = accept(g_listen, 0, 0);
	if(g_conn == FN_BAD) {
		return;
	}
	setsockopt(g_conn, IPPROTO_TCP, TCP_NODELAY, (const char *)&one,
								sizeof(one));
#ifdef SO_NOSIGPIPE
	setsockopt(g_conn, SOL_SOCKET, SO_NOSIGPIPE, (const char *)&one,
								sizeof(one));
#endif
	g_rx_len = 0;
	count_units();
}
