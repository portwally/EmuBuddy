/* Host test for the FujiNet connection (fujinet.c). A thread plays
 * FujiNet-PC: it connects to the listener and answers SmartPort over SLIP.
 *   cmake --build build --target test_fujinet && build/test_fujinet
 * With the word "real" it waits for a real FujiNet-PC instead and lists its
 * units:  build/test_fujinet real */

#include "../fujinet.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT	19850

static int	g_fails;
#define CHECK(c, ...) do { if(!(c)) { printf("FAIL %s:%d: ", __FILE__, \
	__LINE__); printf(__VA_ARGS__); printf("\n"); g_fails++; } } while(0)

/* ---- the fake FujiNet-PC ---- */
#define FAKE_UNITS	3
static int	g_fake_mode;	/* 1: answer READ BLOCK late, 2: hang up on it */
static int	g_fake_inits;
static unsigned char g_fake_written[512];
static int	g_fake_ctl_len;

static void
put_frame(int s, const unsigned char *b, int n)
{
	unsigned char out[4200];
	int	i, k = 0;

	out[k++] = 0xc0;
	for(i = 0; i < n; i++) {
		if(b[i] == 0xc0) { out[k++] = 0xdb; out[k++] = 0xdc; }
		else if(b[i] == 0xdb) { out[k++] = 0xdb; out[k++] = 0xdd; }
		else { out[k++] = b[i]; }
	}
	out[k++] = 0xc0;
	/* in two pieces, to see that the reader puts them together */
	(void)!write(s, out, (size_t)(k / 2));
	usleep(2000);
	(void)!write(s, out + k / 2, (size_t)(k - k / 2));
}

static int
get_frame(int s, unsigned char *b, int max)
{
	unsigned char c;
	int	n = 0, in = 0, esc = 0;

	while(read(s, &c, 1) == 1) {
		if(c == 0xc0) {
			if(in && n > 0) {
				return n;
			}
			in = 1;
			continue;
		}
		if(!in) {
			continue;
		}
		if(esc) {
			c = (c == 0xdc) ? 0xc0 : 0xdb;
			esc = 0;
		} else if(c == 0xdb) {
			esc = 1;
			continue;
		}
		if(n < max) {
			b[n++] = c;
		}
	}
	return -1;
}

static void *
fake_fujinet(void *arg)
{
	struct sockaddr_in a;
	unsigned char req[4200], ans[600];
	int	s, n, i, unit;

	(void)arg;
	s = socket(AF_INET, SOCK_STREAM, 0);
	memset(&a, 0, sizeof(a));
	a.sin_family = AF_INET;
	a.sin_port = htons(PORT);
	a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	while(connect(s, (struct sockaddr *)&a, sizeof(a)) != 0) {
		usleep(10000);
	}
	while((n = get_frame(s, req, sizeof(req))) >= 11) {
		unit = req[3];
		ans[0] = req[0];
		ans[1] = 0;
		switch(req[1]) {
		case FUJINET_INIT:
			g_fake_inits++;
			if(unit > FAKE_UNITS) {
				continue;		/* no answer at all */
			}
			ans[1] = (unit == FAKE_UNITS) ? 0xff : 0;
			put_frame(s, ans, 2);
			break;
		case FUJINET_READ_BLOCK:
			if(g_fake_mode == 2) {
				close(s);
				return 0;
			}
			if(g_fake_mode == 1) {		/* too late for the caller */
				usleep(400000);
			}
			/* a block with the bytes that SLIP has to escape */
			for(i = 0; i < 512; i++) {
				ans[2 + i] = (unsigned char)(i + req[6]);
			}
			ans[2] = 0xc0;
			ans[3] = 0xdb;
			if(req[6] == 99) {		/* past the end */
				ans[1] = 0x27;
			}
			put_frame(s, ans, 514);
			break;
		case FUJINET_WRITE_BLOCK:
			if(n == 11 + 512) {
				memcpy(g_fake_written, req + 11, 512);
			} else {
				ans[1] = 0x27;
			}
			put_frame(s, ans, 2);
			break;
		case FUJINET_CONTROL:
			g_fake_ctl_len = n - 11;
			ans[1] = (req[6] == 0x42 && req[7] == 3) ? 0 : 0x21;
			put_frame(s, ans, 2);
			break;
		case FUJINET_STATUS:
			ans[2] = 0xf8;
			ans[3] = (unsigned char)unit;
			ans[4] = req[6];
			put_frame(s, ans, 5);
			break;
		default:
			ans[1] = 0x01;
			put_frame(s, ans, 2);
			break;
		}
	}
	close(s);
	return 0;
}

static int
real(void)
{
	unsigned char p[5], dib[64];
	int	i, u, n, err;

	if(fujinet_start(FUJINET_PORT) != 0) {
		return 1;
	}
	for(i = 0; i < 300 && !fujinet_units(); i++) {
		fujinet_poll();
		usleep(100000);
	}
	printf("%d units\n", fujinet_units());
	for(u = 1; u <= fujinet_units(); u++) {
		memset(p, 0, sizeof(p));
		p[0] = 3;				/* the DIB */
		err = fujinet_call(FUJINET_STATUS, u, p, 3, 0, 0, dib,
							sizeof(dib), &n);
		if(err || n < 25) {
			printf("unit %2d: error %02x\n", u, err);
			continue;
		}
		printf("unit %2d: %-16.*s type %02x, %d blocks\n", u, dib[4],
			dib + 5, dib[21], dib[1] | (dib[2] << 8) | (dib[3] << 16));
	}
	fujinet_stop();
	return fujinet_units() ? 0 : 0;
}

int
main(int argc, char **argv)
{
	pthread_t th;
	unsigned char p[5], buf[600], blk[512];
	int	i, n, err;

	if(argc > 1 && !strcmp(argv[1], "real")) {
		return real();
	}
	/* 1. nothing connected */
	CHECK(fujinet_units() == 0, "units before start");
	CHECK(fujinet_call(FUJINET_STATUS, 1, 0, 3, 0, 0, buf, sizeof(buf), &n)
				== FUJINET_ERR_NODEV, "call without a FujiNet");
	CHECK(fujinet_start(PORT) == 0, "cannot listen");
	fujinet_poll();
	CHECK(fujinet_units() == 0, "units before a connection");

	/* 2. a connection: the units are counted, the last answers $FF */
	pthread_create(&th, 0, fake_fujinet, 0);
	for(i = 0; i < 300 && !fujinet_units(); i++) {
		fujinet_poll();
		usleep(10000);
	}
	CHECK(fujinet_units() == FAKE_UNITS && g_fake_inits == FAKE_UNITS,
		"%d units after %d INITs", fujinet_units(), g_fake_inits);
	CHECK(fujinet_call(FUJINET_STATUS, FAKE_UNITS + 1, 0, 3, 0, 0, buf,
		sizeof(buf), &n) == FUJINET_ERR_NODEV, "a unit past the last");

	/* 3. STATUS: the parameters arrive, the data comes back */
	memset(p, 0, sizeof(p));
	p[0] = 3;
	err = fujinet_call(FUJINET_STATUS, 2, p, 3, 0, 0, buf, sizeof(buf), &n);
	CHECK(err == 0 && n == 3 && buf[0] == 0xf8 && buf[1] == 2 &&
			buf[2] == 3, "STATUS: err %02x, %d bytes", err, n);

	/* 4. READ BLOCK, with the two bytes SLIP has to escape in it */
	p[0] = 7;
	err = fujinet_call(FUJINET_READ_BLOCK, 1, p, 3, 0, 0, buf, sizeof(buf),
									&n);
	CHECK(err == 0 && n == 512 && buf[0] == 0xc0 && buf[1] == 0xdb &&
		buf[2] == 9 && buf[511] == (unsigned char)(511 + 7),
		"READ BLOCK: err %02x, %d bytes, %02x %02x", err, n, buf[0],
		buf[1]);
	p[0] = 99;		/* an error with data behind it: no data here */
	err = fujinet_call(FUJINET_READ_BLOCK, 1, p, 3, 0, 0, buf, sizeof(buf),
									&n);
	CHECK(err == 0x27 && n == 0, "error block: err %02x, %d bytes", err, n);

	/* 5. WRITE BLOCK and CONTROL carry data out */
	for(i = 0; i < 512; i++) {
		blk[i] = (unsigned char)(i * 5);	/* has C0 and DB in it */
	}
	p[0] = 1;
	err = fujinet_call(FUJINET_WRITE_BLOCK, 1, p, 3, blk, 512, 0, 0, 0);
	CHECK(err == 0 && !memcmp(blk, g_fake_written, 512), "WRITE BLOCK: %02x",
									err);
	p[0] = 0x42;
	p[1] = 3;			/* the network unit */
	err = fujinet_call(FUJINET_CONTROL, 3, p, 3, blk, 20, 0, 0, 0);
	CHECK(err == 0 && g_fake_ctl_len == 20, "CONTROL: err %02x, %d bytes",
							err, g_fake_ctl_len);

	/* 6. an answer that comes too late: the call gives up, and the late
	 * answer is not taken for the answer of the next call */
	fujinet_set_timeout_ms(150);
	g_fake_mode = 1;
	p[0] = 1;
	p[1] = 0;
	err = fujinet_call(FUJINET_READ_BLOCK, 1, p, 3, 0, 0, buf, sizeof(buf),
									&n);
	CHECK(err == FUJINET_ERR_IO && n == 0, "late answer: err %02x", err);
	g_fake_mode = 0;
	fujinet_set_timeout_ms(2000);
	p[0] = 3;
	err = fujinet_call(FUJINET_STATUS, 3, p, 3, 0, 0, buf, sizeof(buf), &n);
	CHECK(err == 0 && n == 3 && buf[1] == 3, "the call after it: err %02x, "
						"%d bytes", err, n);

	/* 7. FujiNet-PC goes away in the middle of a call */
	g_fake_mode = 2;
	err = fujinet_call(FUJINET_READ_BLOCK, 1, p, 3, 0, 0, buf, sizeof(buf),
									&n);
	CHECK(err == FUJINET_ERR_NODEV && fujinet_units() == 0,
		"hang-up: err %02x, %d units", err, fujinet_units());
	pthread_join(th, 0);

	/* 8. and comes back: counted again */
	g_fake_mode = 0;
	g_fake_inits = 0;
	pthread_create(&th, 0, fake_fujinet, 0);
	for(i = 0; i < 300 && !fujinet_units(); i++) {
		fujinet_poll();
		usleep(10000);
	}
	CHECK(fujinet_units() == FAKE_UNITS, "%d units after coming back",
							fujinet_units());
	fujinet_stop();
	CHECK(fujinet_units() == 0, "units after stop");
	pthread_join(th, 0);

	if(g_fails) {
		printf("%d FujiNet tests FAILED\n", g_fails);
		return 1;
	}
	printf("all FujiNet tests passed\n");
	return 0;
}
