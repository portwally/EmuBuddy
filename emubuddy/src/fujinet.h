/* FujiNet for EmuBuddy: the connection to FujiNet-PC by "SmartPort over
 * SLIP" (fujinet.c). See docs/FUJINET_PLAN.md. Knows nothing about the IIgs:
 * smartport.c passes the SmartPort calls for FujiNet's units to it. */
#ifndef EMUBUDDY_FUJINET_H
#define EMUBUDDY_FUJINET_H

/* The commands, as in SmartPort */
#define FUJINET_STATUS		0
#define FUJINET_READ_BLOCK	1
#define FUJINET_WRITE_BLOCK	2
#define FUJINET_FORMAT		3
#define FUJINET_CONTROL		4
#define FUJINET_INIT		5
#define FUJINET_OPEN		6
#define FUJINET_CLOSE		7
#define FUJINET_READ		8
#define FUJINET_WRITE		9

#define FUJINET_PORT		1985	/* FujiNet-PC's default */
#define FUJINET_MAX_DATA	2048	/* the most data one call carries */

/* SmartPort errors this file makes up itself */
#define FUJINET_ERR_IO		0x27	/* no answer in time, or a bad one */
#define FUJINET_ERR_NODEV	0x28	/* no FujiNet connected, or no such unit */

/* Starts listening for FujiNet-PC on 127.0.0.1:port. 0 when it worked. */
int	fujinet_start(int port);
void	fujinet_stop(void);
/* Call now and then (once a frame): takes a new connection and counts its
 * units, and notices when the connection is gone. */
void	fujinet_poll(void);
/* The number of units of the FujiNet that is connected; 0 = none */
int	fujinet_units(void);
/* How long a call waits for its answer (default 10 s) */
void	fujinet_set_timeout_ms(int ms);

/* One SmartPort call for unit (1 = FujiNet's first). params: the 5 bytes
 * after the header (the status or control code and network unit, or the
 * block number, or the byte count and address); pcount: SmartPort's
 * parameter count. out, out_len: data to send (WRITE BLOCK, WRITE, CONTROL).
 * in, in_max: where the answer's data goes; *in_len gets its length.
 * Returns the SmartPort error: 0 fine. */
int	fujinet_call(int cmd, int unit, const unsigned char *params, int pcount,
		const unsigned char *out, int out_len,
		unsigned char *in, int in_max, int *in_len);

#endif
