/* Uthernet II (WIZnet W5100) emulation for EmuBuddy.
 *
 * The card sits in a slot and exposes the W5100's indirect bus interface at
 * $C0n4-$C0n7 (mode, address high, address low, data). This module only knows
 * that 16-byte register window; it has no core or SDL dependencies.
 *
 * Raw Ethernet frames (MACRAW mode on socket 0, what Marinetti's Uthernet II
 * link layer uses) go to and come from a network backend (net_slirp.c). */
#ifndef EMUBUDDY_UTHERNET2_H
#define EMUBUDDY_UTHERNET2_H

#include <stdint.h>

void	uthernet2_reset(void);
/* reg = low nibble of the $C0nX address (0-15) */
int	uthernet2_read(int reg);
void	uthernet2_write(int reg, int val);
/* Call once per main-loop iteration: runs the network backend and moves
 * received frames into the card's receive buffer. */
void	uthernet2_poll(void);
/* Backend -> card: a frame arrived from the network. */
void	uthernet2_rx_frame(const uint8_t *frame, int len);
/* Packet counters for the status bar (sent, received). */
void	uthernet2_counters(unsigned *tx, unsigned *rx);
int	uthernet2_active(void);		/* socket 0 open in MACRAW mode */

/* Network backend (net_slirp.c, or net_none.c without libslirp) */
int	net_init(void);			/* 0 ok */
void	net_send(const uint8_t *frame, int len);
void	net_poll(void);
void	net_shutdown(void);
const char *net_backend_name(void);

#endif
