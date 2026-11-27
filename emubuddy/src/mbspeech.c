/**********************************************************************/
/*                    EmuBuddy - Apple //gs Emulator                  */
/*                    Copyright 2026 EmuBuddy Contributors            */
/*                                                                    */
/*      This code is covered by the GNU GPL v3                        */
/*      See the file COPYING.txt or https://www.gnu.org/licenses/     */
/**********************************************************************/

// The SSI-263 speech chips of the Mockingboard C (docs/SPEECH_PHASOR_PLAN.md).
//
// The chip itself is Tamas Geczy's model in ssi263/ (MIT).  This file wires
// it to the card, after the behaviour AppleWin documents for real cards:
//  . the first speech chip answers writes with address bit 6 set ($Cn40..),
//    and its A/R line goes to CA1 of the second 6522 (pair 1);
//  . the second one answers address bit 5 ($Cn20..), A/R to CA1 of pair 0;
//  . register = address bits 2-0; the chip cannot be read on a Mockingboard;
//  . writes to registers 0, 1, 2 (and 3 with CTL=1) clear the request;
//  . Ctrl-Reset does not reach the chips (only the 6522s are reset);
//  . on a Phasor in native mode the chips can be read (bit 7 = A/R) and
//    A/R drives the IRQ line directly instead of CA1;
//  . mode 00 (DR1:DR0 at CTL 1->0) only keeps A/R from the computer: the
//    phoneme runs in the previous mode and bit 7 still goes up (AppleWin,
//    from real cards).  The chip model gives no A/R in mode 00, so it is
//    given the previous mode, and A/R is not passed on;
//  . CTL 1->0 starts the phoneme in register 0 (A/R down until it ends);
//  . a phoneme repeats until it is changed: when a write to register 1 or 2
//    answers a request, the chip asks again when the phoneme ends again.
//    The chip model keeps its request up after such a write, so the
//    phoneme is written again here (the model holds it until the current
//    one is done), which drops the request and raises it at its end.
//
// Each chip runs in step with emulated time: on every write, on every
// sound_play(), and on a short event while it is awake, it makes the
// samples up to "now" into a FIFO that sound_play() mixes in.

#include "defc.h"
#include "sound.h"
#include "ssi263/ssi263.h"

extern Mockingboard g_mockingboard;
extern double g_dsamps_per_dfcyc;
extern int g_audio_rate;
extern int g_doc_vol;
extern dword64 g_cur_dfcyc;
extern int g_slot4_mockingboard;
extern int g_phasor_mode;

int	g_mb_speech = 1;		// speech chips: 0 none, 1 one, 2 two

#define SPK_FIFO	16384		// samples (about 1/3 s)
#define SPK_CHUNK	512
#define SPK_EVENT_DCYC	256		// awake chips are run every 256 cycles

typedef struct {
	ssi263	*chip;
	double	dsamp_done;		// emulated sample position made so far
	int	pair;			// 6522 whose CA1 gets A/R
	int	request;		// A/R as last seen
	int	powered_down;		// CTL=1
	int	r0;			// last value written to register 0
	int	ints;			// A/R reaches the computer (mode not 00)
	int	mode_prev;		// the last mode other than 00
	float	fifo[SPK_FIFO];
	int	head, count;
} Spk;

static Spk	g_spk[2];

// A/R reaches the computer: CA1 of the chip's 6522, or (Phasor native mode)
//  the IRQ line itself
static void
spk_signal(Spk *k, dword64 dfcyc)
{
	Mos6522	*via;

	if(!k->ints) {			// mode 00: A/R stays on the chip
		return;
	}
	if(mock_phasor_native()) {
		add_irq(IRQ_PENDING_MBSPEECH);
		return;
	}
	if(g_slot4_mockingboard == 2 && g_phasor_mode != 0) {
		return;			// Phasor in Echo+ mode: A/R reaches nothing
	}
	// A/R falls: a negative edge on CA1 (PCR bit 0 = 0)
	via = &(g_mockingboard.pair[k->pair].mos6522);
	if((via->pcr & 1) == 0) {
		via->ifr = mock_6522_new_ifr(dfcyc, k->pair, via->ifr | 0x02,
								via->ier);
	}
}

static void
spk_irq_update(void)
{
	if(!(g_spk[0].request && g_spk[0].ints) &&
				!(g_spk[1].request && g_spk[1].ints)) {
		remove_irq(IRQ_PENDING_MBSPEECH);
	}
}
static int	g_spk_rate = 0;		// rate the chips were made for
static int	g_spk_event = 0;	// an EV_MBSPEECH event is pending
static int	g_spk_log = -1;

static int
spk_logging(void)
{
	const char *e;

	if(g_spk_log < 0) {
		e = getenv("EMUBUDDY_SPEECHLOG");
		g_spk_log = (e && e[0] && e[0] != '0') ? atoi(e) : 0;
		if(g_spk_log == 0 && e && e[0] && e[0] != '0') {
			g_spk_log = 1;
		}
	}
	return g_spk_log;
}

static int
spk_rate(void)
{
	return (g_audio_rate >= 8000) ? g_audio_rate : 44100;
}

static void
spk_make(int i, dword64 dfcyc)
{
	ssi263_params p;
	Spk	*k = &g_spk[i];

	if(k->chip) {
		ssi263_free(k->chip);
	}
	ssi263_default_params(&p);
	k->chip = ssi263_new(&p, ssi263_default_rom(), (double)spk_rate());
	ssi263_write(k->chip, 3, 0x80);		// power-on: power down (CTL=1)
	k->pair = (i == 0) ? 1 : 0;
	k->dsamp_done = (double)(dfcyc * g_dsamps_per_dfcyc);
	k->request = 0;
	k->powered_down = 1;
	k->ints = 1;
	k->mode_prev = 0;
	k->head = 0;
	k->count = 0;
}

static void
spk_ensure(dword64 dfcyc)
{
	int	i;

	if(g_spk_rate != spk_rate() || !g_spk[0].chip) {
		g_spk_rate = spk_rate();
		for(i = 0; i < 2; i++) {
			spk_make(i, dfcyc);
		}
	}
}

static void
spk_push(Spk *k, const double *buf, long n)
{
	long	i;
	int	tail;

	for(i = 0; i < n; i++) {
		if(k->count >= SPK_FIFO) {	// nobody plays: drop the oldest
			k->head = (k->head + 1) % SPK_FIFO;
			k->count--;
		}
		tail = (k->head + k->count) % SPK_FIFO;
		k->fifo[tail] = (float)buf[i];
		k->count++;
	}
}

// Run chip i up to emulated time dfcyc; raise CA1 when A/R comes
static void
spk_advance(int i, dword64 dfcyc)
{
	double	buf[SPK_CHUNK + 1];
	Spk	*k = &g_spk[i];
	double	target;
	long	n, want, got;

	if(!k->chip || g_dsamps_per_dfcyc <= 0.0) {
		return;
	}
	target = (double)(dfcyc * g_dsamps_per_dfcyc);
	n = (long)(target - k->dsamp_done);
	if(n <= 0) {
		return;
	}
	if(n > 4 * SPK_FIFO) {			// a long stop (debugger, pause)
		ssi263_skip(k->chip, (double)(n - SPK_FIFO) / g_spk_rate);
		k->dsamp_done += (double)(n - SPK_FIFO);
		n = SPK_FIFO;
	}
	while(n > 0) {
		want = (n > SPK_CHUNK) ? SPK_CHUNK : n;
		if(k->request) {
			got = ssi263_run(k->chip, want, buf);
		} else {
			got = ssi263_run_until_request(k->chip, want, buf);
		}
		if(got <= 0 && !ssi263_request(k->chip)) {
			break;
		}
		spk_push(k, buf, got);
		k->dsamp_done += (double)got;
		n -= got;
		if(!k->request && ssi263_request(k->chip)) {
			k->request = 1;
			spk_signal(k, dfcyc);
			if(spk_logging()) {
				printf("Speech %d: A/R, %s %d\n", i,
					mock_phasor_native() ? "IRQ" :
					"CA1 of 6522", k->pair);
			}
		}
	}
}

static void
spk_schedule(dword64 dfcyc)
{
	int	i, awake = 0;

	for(i = 0; i < 2; i++) {
		if(g_spk[i].chip && !g_spk[i].powered_down) {
			awake = 1;
		}
	}
	if(awake && !g_spk_event) {
		g_spk_event = 1;
		add_event_mbspeech(dfcyc + ((dword64)SPK_EVENT_DCYC << 16));
	}
}

void
mbspeech_event(dword64 dfcyc)
{
	int	i;

	g_spk_event = 0;
	for(i = 0; i < 2; i++) {
		spk_advance(i, dfcyc);
	}
	spk_schedule(dfcyc);
}

// A write to the Mockingboard at $Cnxx (loc = low 8 bits)
void
mbspeech_write(word32 loc, word32 val, dword64 dfcyc)
{
	int	i, reg;
	Spk	*k;
	Mos6522	*via;

	if(g_mb_speech <= 0) {
		return;
	}
	for(i = 0; i < 2; i++) {
		if(i == 0 && !(loc & 0x40)) {
			continue;
		}
		if(i == 1 && (!(loc & 0x20) || g_mb_speech < 2)) {
			continue;
		}
		spk_ensure(dfcyc);
		k = &g_spk[i];
		spk_advance(i, dfcyc);
		reg = loc & 7;
		if(reg == 3 && k->powered_down && !(val & 0x80)) {
			// CTL 1->0: the mode DR1:DR0 is taken from register 0
			k->ints = (k->r0 >> 6) != 0;
			if(k->ints) {
				k->mode_prev = k->r0 >> 6;
			} else if(k->mode_prev) {
				ssi263_write(k->chip, 0,
					(k->mode_prev << 6) | (k->r0 & 0x3f));
			}
			ssi263_write(k->chip, 3, val & 0xff);
			// and the phoneme in register 0 starts now (as AppleWin's
			//  Play on CTL 1->0), so A/R is down until it ends
			ssi263_write(k->chip, 0, k->r0);
			k->request = ssi263_request(k->chip);
		} else {
			ssi263_write(k->chip, reg, val & 0xff);
		}
		if(reg == 0) {
			k->r0 = val & 0xff;
		} else if((reg == 1 || reg == 2) && k->request &&
						ssi263_request(k->chip)) {
			ssi263_write(k->chip, 0, k->r0);	// repeat
		}
		if(spk_logging()) {
			printf("Speech %d: R%d = %02x\n", i, reg > 4 ? 4 : reg,
								val & 0xff);
		}
		if(reg == 3) {
			k->powered_down = (val & 0x80) ? 1 : 0;
		}
		if(reg <= 2 || (reg == 3 && (val & 0x80))) {
			// the request is answered: A/R rises, CA1 flag cleared
			k->request = ssi263_request(k->chip);
			via = &(g_mockingboard.pair[k->pair].mos6522);
			if(via->ifr & 0x02) {
				via->ifr = mock_6522_new_ifr(dfcyc, k->pair,
						via->ifr & ~0x02, via->ier);
			}
			spk_irq_update();
		}
	}
	spk_schedule(dfcyc);
}

// Phasor native mode: a read of a speech chip, A/R in bit 7 (1 = request);
//  reading does not clear the request
word32
mbspeech_read(word32 loc, dword64 dfcyc)
{
	int	i;

	i = -1;
	if(loc & 0x40) {
		i = 0;
	}
	if((loc & 0x20) && g_mb_speech >= 2) {
		i = 1;
	}
	if(i < 0 || g_mb_speech <= 0 || !g_spk[i].chip) {
		return 0;
	}
	spk_advance(i, dfcyc);
	return g_spk[i].request ? 0x80 : 0;
}

// The Phasor went into or out of native mode: move a pending A/R to the
//  other line
void
mbspeech_card_mode(dword64 dfcyc)
{
	int	i;

	remove_irq(IRQ_PENDING_MBSPEECH);
	for(i = 0; i < 2; i++) {
		if(g_spk[i].chip && g_spk[i].request) {
			spk_signal(&g_spk[i], dfcyc);	// (not in mode 00)
		}
	}
}

// Mix the speech of the stretch sound_play() is making into out[] (stereo)
int
mbspeech_mix(int *out, int num_samps, int zero_first, dword64 dfcyc)
{
	Spk	*k;
	int	i, j, any = 0, n, v;
	float	scale;

	if(!g_slot4_mockingboard || g_mb_speech <= 0 || !g_spk[0].chip) {
		return 0;
	}
	scale = (float)(36000.0 * g_doc_vol / 15.0);
	for(i = 0; i < 2; i++) {
		spk_advance(i, dfcyc);
		k = &g_spk[i];
		if(k->count == 0) {
			continue;
		}
		if(!any && zero_first) {
			memset(out, 0, (size_t)num_samps * 2 * sizeof(int));
		}
		any = 1;
		// the newest num_samps samples belong to this stretch
		while(k->count > num_samps) {
			k->head = (k->head + 1) % SPK_FIFO;
			k->count--;
		}
		n = k->count;
		for(j = num_samps - n; j < num_samps; j++) {
			v = (int)(k->fifo[k->head] * scale);
			k->head = (k->head + 1) % SPK_FIFO;
			out[2 * j] += v;
			out[2 * j + 1] += v;
		}
		k->count = 0;
		if(spk_logging() > 1) {
			int	pk = 0, q;
			for(q = 0; q < num_samps; q++) {
				if(abs(out[2 * q]) > pk) {
					pk = abs(out[2 * q]);
				}
			}
			printf("Speech %d: mixed %d (%d new), peak %d\n", i,
							num_samps, n, pk);
		}
	}
	return any;
}
