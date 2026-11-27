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
// The Votrax SC-01 (phase 4): the speech chip of the Mockingboard
// Sound/Speech I, MAME's votrax.cpp ported to C (votrax/, BSD-3).  As in
// MAME and AppleWin: when the first 6522's PCR sets CB2 to pulse output
// ($B0), a write to its port B is a phoneme for the SC-01 (bits 5-0, the
// inflection in bits 7-6) and does not reach the AY; the SC-01's A/R line
// goes to CB1 of that 6522 (IFR bit 4) and rises when the phoneme ends.
// The chip runs at 1,022,727 Hz.  It needs its ROM (sc01a.bin, 512 bytes),
// which EmuBuddy does not ship: the file of the setting, or sc01a.bin or
// MAME's votrax.zip next to the IIgs ROM.  Without it there is no SC-01.
//
// Each chip runs in step with emulated time: on every write, on every
// sound_play(), and on a short event while it is awake, it makes the
// samples up to "now" into a FIFO that sound_play() mixes in.

#include "defc.h"
#include "sound.h"
#include "ssi263/ssi263.h"
#include "votrax/votrax.h"

extern Mockingboard g_mockingboard;
extern double g_dsamps_per_dfcyc;
extern int g_audio_rate;
extern int g_doc_vol;
extern dword64 g_cur_dfcyc;
extern int g_slot4_mockingboard;
extern int g_phasor_mode;

int	g_mb_speech = 1;		// speech chips: 0 none, 1 one, 2 two
int	g_mb_sc01 = 1;			// the SC-01, when its ROM is found
char	*g_sc01_rom = "";		// config: the SC-01 ROM; "" = search

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
static void	spk_schedule(dword64 dfcyc);
static void	sc01_advance(dword64 dfcyc);
static int	sc01_mix(int *out, int num_samps, int zero_first, dword64 dfcyc);
#define SC01_LEVEL	30000.0		// the SC-01's level: about the SSI-263's

// The Votrax SC-01
#define SC01_CLOCK	1022727.0
static votrax	*g_sc01 = 0;
static int	g_sc01_tried = 0;	// looked for its ROM
static char	g_sc01_dir[1024];	// the IIgs ROM in use: where to look
static double	g_sc01_dsamp_done;
static int	g_sc01_ar = 1;		// A/R as last seen
static float	g_sc01_fifo[SPK_FIFO];
static int	g_sc01_head, g_sc01_count;

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

// ---- the Votrax SC-01 ----

// Where to look for the SC-01 ROM: the folder of the IIgs ROM in use
//  (config.c, when the ROMs are loaded)
void
mbspeech_set_rom_dir(const char *iigs_rom)
{
	snprintf(g_sc01_dir, sizeof(g_sc01_dir), "%s", iigs_rom ? iigs_rom : "");
	g_sc01_tried = 0;
}

// Looks for the ROM: the file of the setting, then sc01a.bin and MAME's
//  votrax.zip next to the IIgs ROM.  1 when found.
static int
sc01_find_rom(byte *rom)
{
	char	path[1100];
	const char *slash;
	int	dirlen;

	if(g_sc01_rom && g_sc01_rom[0] &&
		rom_file_read(g_sc01_rom, "sc01a.bin", rom, VOTRAX_ROM_SIZE) == 0) {
		return 1;
	}
	if(!g_sc01_dir[0]) {
		return 0;
	}
	slash = strrchr(g_sc01_dir, '/');
	dirlen = slash ? (int)(slash - g_sc01_dir + 1) : 0;
	snprintf(path, sizeof(path), "%.*ssc01a.bin", dirlen, g_sc01_dir);
	if(rom_file_read(path, "sc01a.bin", rom, VOTRAX_ROM_SIZE) == 0) {
		return 1;
	}
	snprintf(path, sizeof(path), "%.*svotrax.zip", dirlen, g_sc01_dir);
	return rom_file_read(path, "sc01a.bin", rom, VOTRAX_ROM_SIZE) == 0;
}

// For the Slots window: 1 when the SC-01 ROM is there
int
mbspeech_sc01_rom_found(void)
{
	byte	rom[VOTRAX_ROM_SIZE];

	return g_sc01 != 0 || sc01_find_rom(rom);
}

// The SC-01, made the first time it is needed (NULL without its ROM)
static votrax *
sc01_get(dword64 dfcyc)
{
	byte	rom[VOTRAX_ROM_SIZE];

	if(g_mb_sc01 <= 0) {
		return 0;
	}
	if(g_sc01 && g_spk_rate == spk_rate()) {
		return g_sc01;
	}
	if(g_sc01) {				// a new audio rate
		votrax_free(g_sc01);
		g_sc01 = 0;
		g_sc01_tried = 0;
	}
	if(g_sc01_tried) {
		return 0;
	}
	g_sc01_tried = 1;
	if(!sc01_find_rom(rom)) {
		printf("Mockingboard: no SC-01 ROM (sc01a.bin or votrax.zip)\n");
		return 0;
	}
	g_spk_rate = spk_rate();
	g_sc01 = votrax_new(rom, SC01_CLOCK, (double)g_spk_rate);
	g_sc01_dsamp_done = (double)(dfcyc * g_dsamps_per_dfcyc);
	g_sc01_ar = 1;
	g_sc01_head = g_sc01_count = 0;
	printf("Mockingboard: SC-01 speech chip\n");
	return g_sc01;
}

static void
sc01_push(const double *buf, long n)
{
	long	i;
	int	tail;

	for(i = 0; i < n; i++) {
		if(g_sc01_count >= SPK_FIFO) {
			g_sc01_head = (g_sc01_head + 1) % SPK_FIFO;
			g_sc01_count--;
		}
		tail = (g_sc01_head + g_sc01_count) % SPK_FIFO;
		g_sc01_fifo[tail] = (float)buf[i];
		g_sc01_count++;
	}
}

// Run the SC-01 up to emulated time dfcyc; A/R rising flags CB1
static void
sc01_advance(dword64 dfcyc)
{
	double	buf[SPK_CHUNK + 1];
	double	target;
	long	n, want, got;
	Mos6522	*via;

	if(!g_sc01 || g_dsamps_per_dfcyc <= 0.0) {
		return;
	}
	target = (double)(dfcyc * g_dsamps_per_dfcyc);
	n = (long)(target - g_sc01_dsamp_done);
	if(n > 4 * SPK_FIFO) {			// a long stop: skip the silence
		g_sc01_dsamp_done += (double)(n - SPK_FIFO);
		n = SPK_FIFO;
	}
	while(n > 0) {
		want = (n > SPK_CHUNK) ? SPK_CHUNK : n;
		got = votrax_run(g_sc01, want, buf, !g_sc01_ar);
		sc01_push(buf, got);
		g_sc01_dsamp_done += (double)got;
		n -= got;
		if(!g_sc01_ar && votrax_ar(g_sc01)) {
			g_sc01_ar = 1;
			// A/R rises: a positive edge on CB1 (PCR bit 4 = 1)
			via = &(g_mockingboard.pair[0].mos6522);
			if(via->pcr & 0x10) {
				via->ifr = mock_6522_new_ifr(dfcyc, 0,
						via->ifr | 0x10, via->ier);
			}
			if(spk_logging()) {
				printf("SC-01: A/R, CB1 of 6522 0\n");
			}
		}
	}
}

// A write to port B of the first 6522 while its PCR has CB2 as pulse
//  output: a phoneme for the SC-01.  1 when the SC-01 took it (the AY
//  then sees nothing), 0 when there is no SC-01.
int
mbspeech_sc01_write(word32 val, dword64 dfcyc)
{
	Mos6522	*via;

	if(!sc01_get(dfcyc)) {
		return 0;
	}
	sc01_advance(dfcyc);
	votrax_inflection(g_sc01, (val >> 6) & 3);
	votrax_write(g_sc01, val & 0x3f);
	g_sc01_ar = 0;
	// A/R falls: the phoneme is taken; CB1's flag is cleared
	via = &(g_mockingboard.pair[0].mos6522);
	if(via->ifr & 0x10) {
		via->ifr = mock_6522_new_ifr(dfcyc, 0, via->ifr & ~0x10,
								via->ier);
	}
	if(spk_logging()) {
		printf("SC-01: phoneme %02x, inflection %d\n", val & 0x3f,
							(val >> 6) & 3);
	}
	spk_schedule(dfcyc);
	return 1;
}

// Mix the SC-01's samples of this stretch into out[] (stereo)
static int
sc01_mix(int *out, int num_samps, int zero_first, dword64 dfcyc)
{
	float	scale;
	int	j, n, v;

	if(!g_sc01) {
		return 0;
	}
	sc01_advance(dfcyc);
	if(g_sc01_count == 0) {
		return 0;
	}
	if(zero_first) {
		memset(out, 0, (size_t)num_samps * 2 * sizeof(int));
	}
	while(g_sc01_count > num_samps) {
		g_sc01_head = (g_sc01_head + 1) % SPK_FIFO;
		g_sc01_count--;
	}
	scale = (float)(SC01_LEVEL * g_doc_vol / 15.0);
	n = g_sc01_count;
	for(j = num_samps - n; j < num_samps; j++) {
		v = (int)(g_sc01_fifo[g_sc01_head] * scale);
		g_sc01_head = (g_sc01_head + 1) % SPK_FIFO;
		out[2 * j] += v;
		out[2 * j + 1] += v;
	}
	g_sc01_count = 0;
	return 1;
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
	if(g_sc01 && !g_sc01_ar) {	// the SC-01 speaks a phoneme
		awake = 1;
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
	sc01_advance(dfcyc);
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

	if(!g_slot4_mockingboard) {
		return 0;
	}
	any = sc01_mix(out, num_samps, zero_first, dfcyc);
	if(g_mb_speech <= 0 || !g_spk[0].chip) {
		return any;
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
