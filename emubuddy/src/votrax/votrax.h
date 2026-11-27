// license:BSD-3-Clause
// copyright-holders:Olivier Galibert
/* votrax.h: the Votrax SC-01-A speech chip, a C port of MAME's
 * src/devices/sound/votrax.cpp (Olivier Galibert, BSD-3-Clause) for
 * EmuBuddy.  See README.md in this folder.
 *
 * The chip needs its 512-byte phoneme ROM (sc01a.bin), which the caller
 * supplies.  The host writes phonemes (votrax_write), watches the A/R line
 * (votrax_ar: 1 = the phoneme has ended, the chip wants the next one) and
 * pulls audio at its own rate (votrax_run). */
#ifndef EMUBUDDY_VOTRAX_H
#define EMUBUDDY_VOTRAX_H

#define VOTRAX_ROM_SIZE	512

typedef struct votrax votrax;

/* mainclock: the chip's clock in Hz (720 kHz nominal; the Mockingboard
 * runs it at 1,022,727 Hz); out_rate: the host's sample rate */
votrax	*votrax_new(const unsigned char *rom, double mainclock,
							double out_rate);
void	votrax_free(votrax *v);
void	votrax_reset(votrax *v);
void	votrax_write(votrax *v, int data);		/* phoneme, 6 bits */
void	votrax_inflection(votrax *v, int data);	/* 2 bits */
int	votrax_ar(const votrax *v);
int	votrax_phone(const votrax *v);
/* Makes n samples at the host rate into out (about -1..1).  With
 * stop_on_ar, it stops after the sample in which A/R rises.  Returns the
 * number of samples made. */
long	votrax_run(votrax *v, long n, double *out, int stop_on_ar);

#endif
