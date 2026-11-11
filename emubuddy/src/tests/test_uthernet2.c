/* Uthernet II + slirp test: drives the emulated W5100 only through its
 * 16-byte register window, the way IIgs software does, and checks real
 * network traffic through slirp:
 *   1. reset/probe, MAC, buffer sizes, socket 0 MACRAW open
 *   2. DHCP DISCOVER -> OFFER for 192.168.64.2
 *   3. ARP for the gateway 192.168.64.1 -> reply
 *   4. DNS query for example.com via 192.168.64.1 -> answer
 *   5. TCP SYN to that address, port 80 -> SYN-ACK
 *   5b. the host computer at its fixed address 192.168.64.254: ARP reply, and a
 *       TCP connection to it arrives at this computer's own 127.0.0.1
 *   6. 300 ARP round trips, so TX and RX buffer pointers wrap many times
 * Exit code 0 when everything passes. */
#include "../uthernet2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static int g_fail;
#define CHECK(c, msg) do { if(c) printf("  ok   %s\n", msg); \
	else { printf("  FAIL %s\n", msg); g_fail++; } } while(0)

static const uint8_t MAC[6] = { 0x02, 0x00, 0xDE, 0xAD, 0xBE, 0xEF };
static const uint8_t BCAST[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
static uint8_t	g_gw_mac[6];
static const uint8_t MYIP[4] = { 192, 168, 64, 2 }, GWIP[4] = { 192, 168, 64, 1 };
static const uint8_t DNSIP[4] = { 192, 168, 64, 1 };

/* ---- register helpers, like a 6502 driver ---- */
static void setaddr(int a) { uthernet2_write(5, a >> 8); uthernet2_write(6, a & 0xff); }
static void wr(int a, int v) { setaddr(a); uthernet2_write(7, v); }
static int rd(int a) { setaddr(a); return uthernet2_read(7); }
static int rd16(int a) { return (rd(a) << 8) | rd(a + 1); }
static void wr16(int a, int v) { wr(a, v >> 8); wr(a + 1, v & 0xff); }

static void
send_frame(const uint8_t *f, int len)
{
	int	w = rd16(0x0424), i;

	setaddr(0x4000 + (w & 0x1fff));		/* socket 0 has 8 KB TX */
	for(i = 0; i < len; i++) {
		uthernet2_write(7, f[i]);		/* auto-increment wraps */
	}
	wr16(0x0424, (w + len) & 0xffff);
	wr(0x0401, 0x20);				/* SEND */
	while(rd(0x0401)) ;
	wr(0x0402, 0x10);				/* clear SEND_OK */
}

/* Wait up to ms for a frame; returns its length or 0. */
static int
recv_frame(uint8_t *f, int max, int ms)
{
	int	t, rsr, r, len, i;

	for(t = 0; t < ms; t += 5) {
		uthernet2_poll();
		rsr = rd16(0x0426);
		if(rsr > 0) {
			r = rd16(0x0428);
			setaddr(0x6000 + (r & 0x1fff));
			len = uthernet2_read(7) << 8;
			len |= uthernet2_read(7);
			for(i = 0; i < len - 2; i++) {
				int b = uthernet2_read(7);
				if(i < max) f[i] = (uint8_t)b;
			}
			wr16(0x0428, (r + len) & 0xffff);
			wr(0x0401, 0x40);		/* RECV */
			return len - 2;
		}
		usleep(5000);
	}
	return 0;
}

/* ---- packet building ---- */
static uint16_t
csum(const uint8_t *p, int n, uint32_t sum)
{
	int	i;

	for(i = 0; i + 1 < n; i += 2) sum += (p[i] << 8) | p[i + 1];
	if(n & 1) sum += p[n - 1] << 8;
	while(sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
	return (uint16_t)~sum;
}

static int
eth(uint8_t *f, const uint8_t *dst, int type)
{
	memcpy(f, dst, 6);
	memcpy(f + 6, MAC, 6);
	f[12] = type >> 8;
	f[13] = type & 0xff;
	return 14;
}

/* IPv4 header at f, payload length plen, returns 20 */
static int
ipv4(uint8_t *f, const uint8_t *src, const uint8_t *dst, int proto, int plen)
{
	uint16_t c;

	memset(f, 0, 20);
	f[0] = 0x45;
	f[2] = (20 + plen) >> 8;
	f[3] = (20 + plen) & 0xff;
	f[8] = 64;
	f[9] = proto;
	memcpy(f + 12, src, 4);
	memcpy(f + 16, dst, 4);
	c = csum(f, 20, 0);
	f[10] = c >> 8;
	f[11] = c & 0xff;
	return 20;
}

static int
udp(uint8_t *f, const uint8_t *src, const uint8_t *dst, int sp, int dp,
				const uint8_t *data, int dlen)
{
	int	n = eth(f, memcmp(dst, "\xff\xff\xff\xff", 4) ? g_gw_mac : BCAST,
								0x0800);
	n += ipv4(f + n, src, dst, 17, 8 + dlen);
	f[n] = sp >> 8; f[n + 1] = sp & 0xff;
	f[n + 2] = dp >> 8; f[n + 3] = dp & 0xff;
	f[n + 4] = (8 + dlen) >> 8; f[n + 5] = (8 + dlen) & 0xff;
	f[n + 6] = f[n + 7] = 0;			/* no UDP checksum */
	memcpy(f + n + 8, data, (size_t)dlen);
	return n + 8 + dlen;
}

static int
arp_req(uint8_t *f, const uint8_t *target)
{
	int	n = eth(f, BCAST, 0x0806);
	static const uint8_t hdr[8] = { 0, 1, 8, 0, 6, 4, 0, 1 };

	memcpy(f + n, hdr, 8);
	memcpy(f + n + 8, MAC, 6);
	memcpy(f + n + 14, MYIP, 4);
	memset(f + n + 18, 0, 6);
	memcpy(f + n + 24, target, 4);
	return n + 28;
}

/* Receive frames until one matches; returns its length. */
static int
wait_for(uint8_t *f, int ms, int (*match)(const uint8_t *, int))
{
	int	t, len;

	for(t = 0; t < ms; t += 50) {
		len = recv_frame(f, 1600, 50);
		if(len && match(f, len)) {
			return len;
		}
	}
	return 0;
}

static int is_dhcp_offer(const uint8_t *f, int n)
{
	return n > 282 && f[12] == 8 && f[13] == 0 && f[23] == 17 &&
		f[34] == 0 && f[35] == 67 && f[42] == 2;
}
static int is_arp_reply(const uint8_t *f, int n)
{
	return n >= 42 && f[12] == 8 && f[13] == 6 && f[21] == 2;
}
static int is_dns_reply(const uint8_t *f, int n)
{
	return n > 54 && f[23] == 17 && f[34] == 0 && f[35] == 53;
}
static int is_synack(const uint8_t *f, int n)
{
	return n >= 54 && f[23] == 6 && (f[47] & 0x12) == 0x12;
}
static const uint8_t HOSTIP[4] = { 192, 168, 64, 254 };
static int is_synack_host(const uint8_t *f, int n)
{
	return is_synack(f, n) && !memcmp(f + 26, HOSTIP, 4);
}

/* A TCP SYN from us to dst:dport; returns the frame length. */
static int
tcp_syn(uint8_t *f, const uint8_t *dmac, const uint8_t *dst, int sport, int dport)
{
	uint8_t	tcp[20], ph[12];
	uint32_t s = 0;
	uint16_t c;
	int	n, i;

	memset(tcp, 0, 20);
	tcp[0] = sport >> 8; tcp[1] = sport & 0xff;
	tcp[2] = dport >> 8; tcp[3] = dport & 0xff;
	tcp[4] = 0x11; tcp[5] = 0x22; tcp[6] = 0x33; tcp[7] = 0x44;
	tcp[12] = 0x50; tcp[13] = 0x02;		/* SYN */
	tcp[14] = 0x20; tcp[15] = 0x00;		/* window 8192 */
	memcpy(ph, MYIP, 4); memcpy(ph + 4, dst, 4);
	ph[8] = 0; ph[9] = 6; ph[10] = 0; ph[11] = 20;
	for(i = 0; i < 12; i += 2) s += (ph[i] << 8) | ph[i + 1];
	c = csum(tcp, 20, s);
	tcp[16] = c >> 8; tcp[17] = c & 0xff;
	n = eth(f, dmac, 0x0800);
	n += ipv4(f + n, MYIP, dst, 6, 20);
	memcpy(f + n, tcp, 20);
	return n + 20;
}

int
main(void)
{
	uint8_t	f[1600], d[600];
	int	n, len, i, ok;
	uint8_t	web[4] = { 0, 0, 0, 0 };

	printf("Uthernet II register + slirp test\n");
	/* 1. reset and setup */
	uthernet2_reset();
	uthernet2_write(4, 0x80);			/* software reset */
	uthernet2_write(4, 0x03);			/* indirect + auto-inc */
	CHECK(uthernet2_read(4) == 0x03, "mode register reads back");
	for(i = 0; i < 6; i++) wr(0x0009 + i, MAC[i]);
	CHECK(rd(0x000E) == 0xEF, "MAC address stored");
	wr(0x001A, 0x03);				/* RMSR: 8K socket 0 */
	wr(0x001B, 0x03);				/* TMSR */
	wr(0x0400, 0x44);				/* MACRAW + MAC filter */
	wr(0x0401, 0x01);				/* OPEN */
	CHECK(rd(0x0403) == 0x42, "socket 0 is in MACRAW mode");
	CHECK(rd(0x0401) == 0, "command register reads 0 when done");
	CHECK(rd16(0x0420) == 8192, "8 KB transmit space free");

	/* 2. DHCP */
	memset(d, 0, 300);
	d[0] = 1; d[1] = 1; d[2] = 6;			/* BOOTREQUEST, ether */
	d[4] = 0x12; d[5] = 0x34; d[6] = 0x56; d[7] = 0x78;	/* xid */
	d[10] = 0x80;					/* broadcast flag */
	memcpy(d + 28, MAC, 6);
	d[236] = 99; d[237] = 130; d[238] = 83; d[239] = 99;	/* cookie */
	d[240] = 53; d[241] = 1; d[242] = 1;		/* DHCPDISCOVER */
	d[243] = 255;
	memset(g_gw_mac, 0xff, 6);
	n = udp(f, (const uint8_t *)"\0\0\0\0", (const uint8_t *)"\xff\xff\xff\xff",
							68, 67, d, 300);
	send_frame(f, n);
	len = wait_for(f, 3000, is_dhcp_offer);
	CHECK(len && !memcmp(f + 42 + 16, MYIP, 4), "DHCP offer for 192.168.64.2");

	/* 3. ARP the gateway */
	n = arp_req(f, GWIP);
	send_frame(f, n);
	len = wait_for(f, 3000, is_arp_reply);
	CHECK(len && !memcmp(f + 28, GWIP, 4), "ARP reply from 192.168.64.1");
	if(len) memcpy(g_gw_mac, f + 22, 6);

	/* 4. DNS: example.com A */
	{
		static const uint8_t q[] = { 0xab, 0xcd, 1, 0, 0, 1, 0, 0, 0, 0,
			0, 0, 7, 'e','x','a','m','p','l','e', 3, 'c','o','m', 0,
			0, 1, 0, 1 };

		n = udp(f, MYIP, DNSIP, 40000, 53, q, sizeof(q));
		send_frame(f, n);
		len = wait_for(f, 5000, is_dns_reply);
		ok = len && ((f[42 + 6] << 8) | f[42 + 7]) > 0;
		CHECK(ok, "DNS answer for example.com via 192.168.64.1");
		/* first A record: scan answers for type 1, len 4 */
		for(i = 42 + 12 + sizeof(q) - 12; ok && i + 16 <= len; i++) {
			if(f[i] == 0xc0 && f[i + 2] == 0 && f[i + 3] == 1 &&
					f[i + 10] == 0 && f[i + 11] == 4) {
				memcpy(web, f + i + 12, 4);
				break;
			}
		}
		printf("       example.com = %d.%d.%d.%d\n", web[0], web[1],
							web[2], web[3]);
	}

	/* 5. TCP SYN -> SYN-ACK */
	if(web[0]) {
		n = tcp_syn(f, g_gw_mac, web, 50000, 80);
		send_frame(f, n);
		len = wait_for(f, 8000, is_synack);
		CHECK(len > 0, "TCP SYN-ACK from example.com:80");
	} else {
		CHECK(0, "TCP test skipped (no DNS answer)");
	}

	/* 5b. the host computer at its fixed address: programs in the IIgs reach
	 * servers that listen on this computer's 127.0.0.1 (the web gateway) */
	{
		uint8_t	hmac[6];
		struct sockaddr_in sa;
		socklen_t sl = sizeof(sa);
		int	ls = socket(AF_INET, SOCK_STREAM, 0), cs = -1, port, t;

		memset(&sa, 0, sizeof(sa));
		sa.sin_family = AF_INET;
		sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		bind(ls, (struct sockaddr *)&sa, sizeof(sa));	/* port 0: any free one */
		listen(ls, 1);
		getsockname(ls, (struct sockaddr *)&sa, &sl);
		port = ntohs(sa.sin_port);
		fcntl(ls, F_SETFL, O_NONBLOCK);

		n = arp_req(f, HOSTIP);
		send_frame(f, n);
		len = wait_for(f, 3000, is_arp_reply);
		CHECK(len && !memcmp(f + 28, HOSTIP, 4),
				"ARP reply from 192.168.64.254 (the host computer)");
		memcpy(hmac, len ? f + 22 : g_gw_mac, 6);
		n = tcp_syn(f, hmac, HOSTIP, 50001, port);
		send_frame(f, n);
		len = wait_for(f, 4000, is_synack_host);
		for(t = 0; t < 100 && cs < 0; t++) {
			uthernet2_poll();
			cs = accept(ls, NULL, NULL);
			if(cs < 0) usleep(10000);
		}
		CHECK(len > 0, "TCP SYN-ACK from 192.168.64.254");
		CHECK(cs >= 0, "that connection arrived at this computer's 127.0.0.1");
		if(cs >= 0) close(cs);
		close(ls);
		/* the gateway and DNS address must still answer */
		n = arp_req(f, GWIP);
		send_frame(f, n);
		CHECK(wait_for(f, 3000, is_arp_reply) > 0, "192.168.64.1 still answers ARP");
	}

	/* 6. many round trips: pointers wrap around both buffers */
	ok = 0;
	for(i = 0; i < 300; i++) {
		n = arp_req(f, GWIP);
		send_frame(f, n);
		if(wait_for(f, 2000, is_arp_reply)) ok++;
	}
	CHECK(ok == 300, "300 ARP round trips (buffers wrapped)");
	printf("       TX_WR=%04x RX_RD=%04x\n", rd16(0x0424), rd16(0x0428));

	net_shutdown();
	printf(g_fail ? "FAILED (%d)\n" : "ALL PASSED\n", g_fail);
	return g_fail ? 1 : 0;
}
