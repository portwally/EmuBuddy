// license:BSD-3-Clause
// copyright-holders:Olivier Galibert
/***************************************************************************

    votrax.c

    Votrax SC01A simulation

    A C port of MAME's src/devices/sound/votrax.cpp for EmuBuddy.  The
    chip logic, the ROM decoding, the filters and their constants are
    MAME's, line for line; what changed is the frame around them: MAME's
    device, stream and timer became a plain struct, a sample loop and a
    tick counter, and the chip's own sample rate (main clock / 18) is
    averaged down to the host's rate.  MAME's explanation of the filter
    mathematics is kept below.

***************************************************************************/

/*
  tp3 stb i1 i2 tp2
  1   1   o  o  white noise
  1   0   -  1  phone timing clock
  1   0   1  0  closure tick
  1   0   0  0  sram write pulse
  0   -   -  -  sram write pulse

i1.o = glottal impulse
i2.o = white noise

tp1 = phi clock (tied to f2q rom access)
*/

#include "votrax.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

// Possible timer parameters
enum {
	T_NONE,
	T_COMMIT_PHONE,
	T_END_OF_PHONE
};

struct votrax {
	unsigned char rom[VOTRAX_ROM_SIZE];	// Internal ROM
	double m_mainclock;                     // Current main clock
	double m_sclock;                        // Stream sample clock (main/18)
	double m_cclock;                        // Capacitor switching clock (main/36)
	u32 m_sample_count;                     // Sample counter, to cadence chip updates

	// The timer, in main clock ticks
	double m_timer_left;
	int m_timer_param;

	// The host side: chip samples averaged into host samples
	double m_out_rate;
	double m_out_pos;                       // chip samples into the current host sample
	double m_out_sum;
	double m_out_last;                      // the last host sample (host rate above the chip's)

	// Inputs
	u8 m_inflection;                        // 2-bit inflection value
	u8 m_phone;                             // 6-bit phone value

	// Outputs
	int m_ar_state;                         // Current ar state

	// "Unpacked" current rom values
	u8 m_rom_duration;                      // Duration in 5KHz units (main/144) of one tick, 16 ticks per phone, 7 bits
	u8 m_rom_vd, m_rom_cld;                 // Duration in ticks of the "voice" and "closure" delays, 4 bits
	u8 m_rom_fa, m_rom_fc, m_rom_va;        // Analog parameters, noise volume, noise freq cutoff and voice volume, 4 bits each
	u8 m_rom_f1, m_rom_f2, m_rom_f2q, m_rom_f3; // Analog parameters, formant frequencies and Q, 4 bits each
	int m_rom_closure;                      // Closure bit, true = silence at cld
	int m_rom_pause;                        // Pause bit

	// Current interpolated values (8 bits each)
	u8 m_cur_fa, m_cur_fc, m_cur_va;
	u8 m_cur_f1, m_cur_f2, m_cur_f2q, m_cur_f3;

	// Current committed values
	u8 m_filt_fa, m_filt_fc, m_filt_va;     // Analog parameters, noise volume, noise freq cutoff and voice volume, 4 bits each
	u8 m_filt_f1, m_filt_f2, m_filt_f2q, m_filt_f3; // Analog parameters, formant frequencies/Q on 4 bits except f2 on 5 bits

	// Internal counters
	u16 m_phonetick;                        // 9-bits phone tick duration counter
	u8  m_ticks;                            // 5-bits tick counter
	u8  m_pitch;                            // 7-bits pitch counter
	u8  m_closure;                          // 5-bits glottal closure counter
	u8  m_update_counter;                   // 6-bits counter for the 625Hz (main/1152) and 208Hz (main/3456) update timing generators

	// Internal state
	int m_cur_closure;                      // Current internal closure state
	u16 m_noise;                            // 15-bit noise shift register
	int m_cur_noise;                        // Current noise output

	// Filter coefficients and level histories
	double m_voice_1[4];
	double m_voice_2[4];
	double m_voice_3[4];

	double m_noise_1[3];
	double m_noise_2[3];
	double m_noise_3[2];
	double m_noise_4[2];

	double m_vn_1[4];
	double m_vn_2[4];
	double m_vn_3[4];
	double m_vn_4[4];
	double m_vn_5[2];
	double m_vn_6[2];

	double m_f1_a[4],  m_f1_b[4];           // F1 filtering
	double m_f2v_a[4], m_f2v_b[4];          // F2 voice filtering
	double m_f2n_a[2], m_f2n_b[2];          // F2 noise filtering
	double m_f3_a[4],  m_f3_b[4];           // F3 filtering
	double m_f4_a[4],  m_f4_b[4];           // F4 filtering
	double m_fx_a[1],  m_fx_b[2];           // Final filtering
	double m_fn_a[3],  m_fn_b[3];           // Noise shaping
};

// This waveform is built using a series of transistors as a resistor
// ladder.  There is first a transistor to ground, then a series of
// seven transistors one quarter the size of the first one, then it
// finishes by an active resistor to +9V.
//
// The terminal of the transistor to ground is used as a middle value.
// Index 0 is at that value. Index 1 is at 0V.  Index 2 to 8 start at
// just after the resistor down the latter.  Indices 9+ are the middle
// value again.
//
// For simplicity, we rescale the values to get the middle at 0 and
// the top at 1.  The final wave is very similar to the patent
// drawing.

static const double s_glottal_wave[9] =
{
	0,
	-4/7.0,
	7/7.0,
	6/7.0,
	5/7.0,
	4/7.0,
	3/7.0,
	2/7.0,
	1/7.0
};

// Compute a total capacitor value based on which bits are currently active
static double bits_to_caps(u32 value, const double *caps_values, int n)
{
	double total = 0;
	int i;
	for(i = 0; i < n; i++) {
		if(value & 1)
			total += caps_values[i];
		value >>= 1;
	}
	return total;
}

static const double s_caps_f1[4]  = { 2546, 4973, 9861, 19724 };
static const double s_caps_f2q[4] = { 1390, 2965, 5875, 11297 };
static const double s_caps_f2[5]  = { 833, 1663, 3164, 6327, 12654 };
static const double s_caps_f3[4]  = { 2226, 4485, 9056, 18111 };

// Shift a history of values by one and insert the new value at the front
static void shift_hist(double val, double *hist_array, int n)
{
	int i;
	for(i = n-1; i > 0; i--)
		hist_array[i] = hist_array[i-1];
	hist_array[0] = val;
}

// Apply a filter and compute the result. 'a' is applied to x (inputs) and 'b' to y (outputs)
static double apply_filter(const double *x, const double *y, const double *a, int na, const double *b, int nb)
{
	double total = 0;
	int i;
	for(i = 0; i < na; i++)
		total += x[i] * a[i];
	for(i = 1; i < nb; i++)
		total -= y[i-1] * b[i];
	return total / b[0];
}

// bitswap(val, b3, b2, b1, b0): bit b3 of val becomes the top bit
static u32 bitswap4(u64 val, int b3, int b2, int b1, int b0)
{
	return (u32)((((val >> b3) & 1) << 3) | (((val >> b2) & 1) << 2) |
				(((val >> b1) & 1) << 1) | ((val >> b0) & 1));
}

static void build_standard_filter(votrax *v, double *a, double *b,
		double c1t, double c1b, double c2t, double c2b, double c3, double c4);
static void build_noise_shaper_filter(votrax *v, double *a, double *b,
		double c1, double c2t, double c2b, double c3, double c4);
static void build_lowpass_filter(votrax *v, double *a, double *b,
		double c1t, double c1b);
static void build_injection_filter(votrax *v, double *a, double *b,
		double c1b, double c2t, double c2b, double c3, double c4);

static void phone_commit(votrax *v)
{
	int i, k;

	// Only these two counters are reset on phone change, the rest is
	// free-running.
	v->m_phonetick = 0;
	v->m_ticks = 0;

	// In the real chip, the rom is re-read all the time.  Since it's
	// internal and immutable, no point in not caching it though.
	for(i = 0; i < 64; i++) {
		u64 val = 0;
		for(k = 0; k < 8; k++)		// ROM_REGION64_LE
			val |= (u64)v->rom[i*8 + k] << (8*k);
		if(v->m_phone == ((val >> 56) & 0x3f)) {
			v->m_rom_f1  = bitswap4(val,  0,  7, 14, 21);
			v->m_rom_va  = bitswap4(val,  1,  8, 15, 22);
			v->m_rom_f2  = bitswap4(val,  2,  9, 16, 23);
			v->m_rom_fc  = bitswap4(val,  3, 10, 17, 24);
			v->m_rom_f2q = bitswap4(val,  4, 11, 18, 25);
			v->m_rom_f3  = bitswap4(val,  5, 12, 19, 26);
			v->m_rom_fa  = bitswap4(val,  6, 13, 20, 27);

			// These two values have their bit orders inverted
			// compared to everything else due to a bug in the
			// prototype (miswiring of the comparator with the ticks
			// count) they compensated in the rom.

			v->m_rom_cld = bitswap4(val, 34, 32, 30, 28);
			v->m_rom_vd  = bitswap4(val, 35, 33, 31, 29);

			v->m_rom_closure  = (val >> 36) & 1;
			{
				u64 nv = ~val;
				v->m_rom_duration = (u8)((((nv >> 37) & 1) << 6) |
					(((nv >> 38) & 1) << 5) | (((nv >> 39) & 1) << 4) |
					(((nv >> 40) & 1) << 3) | (((nv >> 41) & 1) << 2) |
					(((nv >> 42) & 1) << 1) | ((nv >> 43) & 1));
			}

			// Hard-wired on the die, not an actual part of the rom.
			v->m_rom_pause = (v->m_phone == 0x03) || (v->m_phone == 0x3e);

			// That does not happen in the sc01(a) rom, but let's
			// cover our behind.
			if(v->m_rom_vd == 0)
				v->m_cur_closure = v->m_rom_closure;

			return;
		}
	}
}

static void interpolate(u8 *reg, u8 target)
{
	// One step of interpolation, adds one eight of the distance
	// between the current value and the target.
	*reg = (u8)(*reg - (*reg >> 3) + (target << 1));
}

static void filters_commit(votrax *v, int force)
{
	v->m_filt_fa = v->m_cur_fa >> 4;
	v->m_filt_fc = v->m_cur_fc >> 4;
	v->m_filt_va = v->m_cur_va >> 4;

	if(force || v->m_filt_f1 != v->m_cur_f1 >> 4) {
		v->m_filt_f1 = v->m_cur_f1 >> 4;

		build_standard_filter(v, v->m_f1_a, v->m_f1_b,
							  11247,
							  11797,
							  949,
							  52067,
							  2280 + bits_to_caps(v->m_filt_f1, s_caps_f1, 4),
							  166272);
	}

	if(force || v->m_filt_f2 != v->m_cur_f2 >> 3 || v->m_filt_f2q != v->m_cur_f2q >> 4) {
		v->m_filt_f2 = v->m_cur_f2 >> 3;
		v->m_filt_f2q = v->m_cur_f2q >> 4;

		build_standard_filter(v, v->m_f2v_a, v->m_f2v_b,
							  24840,
							  29154,
							  829 + bits_to_caps(v->m_filt_f2q, s_caps_f2q, 4),
							  38180,
							  2352 + bits_to_caps(v->m_filt_f2, s_caps_f2, 5),
							  34270);

		build_injection_filter(v, v->m_f2n_a, v->m_f2n_b,
							   29154,
							   829 + bits_to_caps(v->m_filt_f2q, s_caps_f2q, 4),
							   38180,
							   2352 + bits_to_caps(v->m_filt_f2, s_caps_f2, 5),
							   34270);
	}

	if(force || v->m_filt_f3 != v->m_cur_f3 >> 4) {
		v->m_filt_f3 = v->m_cur_f3 >> 4;
		build_standard_filter(v, v->m_f3_a, v->m_f3_b,
							  0,
							  17594,
							  868,
							  18828,
							  8480 + bits_to_caps(v->m_filt_f3, s_caps_f3, 4),
							  50019);
	}

	if(force) {
		build_standard_filter(v, v->m_f4_a, v->m_f4_b,
							  0,
							  28810,
							  1165,
							  21457,
							  8558,
							  7289);

		build_lowpass_filter(v, v->m_fx_a, v->m_fx_b,
							 1122,
							 23131);

		build_noise_shaper_filter(v, v->m_fn_a, v->m_fn_b,
								  15500,
								  14854,
								  8450,
								  9523,
								  14083);
	}
}

static void chip_update(votrax *v)
{
	int tick_625, tick_208, inp;

	// Phone tick counter update.  Stopped when ticks reach 16.
	// Technically the counter keeps updating, but the comparator is
	// disabled.
	if(v->m_ticks != 0x10) {
		v->m_phonetick++;
		// Comparator is with duration << 2, but there's a one-tick
		// delay in the path.
		if(v->m_phonetick == ((v->m_rom_duration << 2) | 1)) {
			v->m_phonetick = 0;
			v->m_ticks++;
			// The closure latches on the same delay that gates the
			// noise amplitude (m_rom_vd here).  US 4,433,210 gives the
			// closure and the fricative amplitude one shared delay and
			// the vocal amplitude a delay of its own; latching on
			// m_rom_cld, the field that gates the voice amplitude, kept
			// a stop's closure on well into the next phone, after its
			// noise had decayed, so P, T and K lost their release burst.
			if(v->m_ticks == v->m_rom_vd)
				v->m_cur_closure = v->m_rom_closure;
		}
	}

	// The two update timing counters.  One divides by 16, the other
	// by 48, and they're phased so that the 208Hz counter ticks
	// exactly between two 625Hz ticks.
	v->m_update_counter++;
	if(v->m_update_counter == 0x30)
		v->m_update_counter = 0;

	tick_625 = !(v->m_update_counter & 0xf);
	tick_208 = v->m_update_counter == 0x28;

	// Formant update.  Die bug there: fc should be updated, not va.
	// The formants are frozen on a pause phone unless both voice and
	// noise volumes are zero.
	if(tick_208 && (!v->m_rom_pause || !(v->m_filt_fa || v->m_filt_va))) {
		// interpolate(m_cur_va,  m_rom_va);
		// fc moves with fa, below
		interpolate(&v->m_cur_f1,  v->m_rom_f1);
		interpolate(&v->m_cur_f2,  v->m_rom_f2);
		interpolate(&v->m_cur_f2q, v->m_rom_f2q);
		interpolate(&v->m_cur_f3,  v->m_rom_f3);
	}

	// Non-formant update. Same bug there, va should be updated, not fc.
	if(tick_625) {
		if(v->m_ticks >= v->m_rom_vd) {
			interpolate(&v->m_cur_fa, v->m_rom_fa);
			// fc routes the noise, so it moves with the noise amplitude:
			// fast and on the same delay.  On the slow formant update a
			// stop's fc was still drifting toward its own value when the
			// stop released, so its noise took the wrong path.
			interpolate(&v->m_cur_fc, v->m_rom_fc);
		}
		if(v->m_ticks >= v->m_rom_cld) {
			// interpolate(m_cur_fc, m_rom_fc);
			interpolate(&v->m_cur_va, v->m_rom_va);
		}
	}

	// Closure counter, reset every other tick in theory when not
	// active (on the extra rom cycle).
	//
	// The closure level is immediately used in the analog path,
	// there's no pitch synchronization.

	if(!v->m_cur_closure && (v->m_filt_fa || v->m_filt_va))
		v->m_closure = 0;
	else if(v->m_closure != 7 << 2)
		v->m_closure ++;

	// Pitch counter.  Equality comparison, so it's possible to make
	// it miss by manipulating the inflection inputs, but it'll wrap.
	// There's a delay, hence the +2.

	// Intrinsically pre-divides by two, so we added one bit on the 7

	v->m_pitch = (v->m_pitch + 1) & 0xff;
	if(v->m_pitch == ((0xe0 ^ (v->m_inflection << 5) ^ (v->m_filt_f1 << 1)) + 2))
		v->m_pitch = 0;

	// Filters are updated in index 1 of the pitch wave, which does
	// indeed mean four times in a row.
	if((v->m_pitch & 0xf9) == 0x08)
		filters_commit(v, 0);

	// Noise shift register.  15 bits, with a nxor on the last two
	// bits for the loop.
	inp = v->m_cur_noise && (v->m_noise != 0x7fff);
	v->m_noise = ((v->m_noise << 1) & 0x7ffe) | inp;
	v->m_cur_noise = !(((v->m_noise >> 14) ^ (v->m_noise >> 13)) & 1);
}

static double analog_calc(votrax *v)
{
	double vv, n, n2, vn;

	// Voice-only path.
	// 1. Pick up the pitch wave

	vv = v->m_pitch >= (9 << 3) ? 0 : s_glottal_wave[v->m_pitch >> 3];

	// 2. Multiply by the initial amplifier.  It's linear on the die,
	// even if it's not in the patent.
	vv = vv * v->m_filt_va / 15.0;
	shift_hist(vv, v->m_voice_1, 4);

	// 3. Apply the f1 filter
	vv = apply_filter(v->m_voice_1, v->m_voice_2, v->m_f1_a, 4, v->m_f1_b, 4);
	shift_hist(vv, v->m_voice_2, 4);

	// 4. Apply the f2 filter, voice half
	vv = apply_filter(v->m_voice_2, v->m_voice_3, v->m_f2v_a, 4, v->m_f2v_b, 4);
	shift_hist(vv, v->m_voice_3, 4);

	// Noise-only path
	// 5. Pick up the noise pitch.  Amplitude is linear.  Base
	// intensity checked against line-in SC-01-A recordings: with the
	// second insertion below corrected, half the previous level.
	n = 5e3 * (((v->m_pitch & 0x40) ? v->m_cur_noise : 0) ? 1 : -1);
	n = n * v->m_filt_fa / 15.0;
	shift_hist(n, v->m_noise_1, 3);

	// 6. Apply the noise shaper
	n = apply_filter(v->m_noise_1, v->m_noise_2, v->m_fn_a, 3, v->m_fn_b, 3);
	shift_hist(n, v->m_noise_2, 3);

	// 7. Scale with the f2 noise input
	n2 = n * v->m_filt_fc / 15.0;
	shift_hist(n2, v->m_noise_3, 2);

	// 8. Apply the f2 filter, noise half,
	n2 = apply_filter(v->m_noise_3, v->m_noise_4, v->m_f2n_a, 2, v->m_f2n_b, 2);
	shift_hist(n2, v->m_noise_4, 2);

	// Mixed path
	// 9. Add the f2 voice and f2 noise outputs
	vn = vv + n2;
	shift_hist(vn, v->m_vn_1, 4);

	// 10. Apply the f3 filter
	vn = apply_filter(v->m_vn_1, v->m_vn_2, v->m_f3_a, 4, v->m_f3_b, 4);
	shift_hist(vn, v->m_vn_2, 4);

	// 11. Second noise insertion, complementary to the f2 injection
	// (step 7): fc=15 phones (P, SH, CH) send their noise through f2
	// only, fc=0 phones (S, Z, T) through here.  The previous
	// (5 + (15^fc))/20 put a quarter of every phone's noise here, which
	// made a P's release as loud and bright as a T's.
	vn += n * (15^v->m_filt_fc)/15.0;
	shift_hist(vn, v->m_vn_3, 4);

	// 12. Apply the f4 filter
	vn = apply_filter(v->m_vn_3, v->m_vn_4, v->m_f4_a, 4, v->m_f4_b, 4);
	shift_hist(vn, v->m_vn_4, 4);

	// 13. Apply the glottal closure amplitude, also linear
	vn = vn * (7 ^ (v->m_closure >> 2)) / 7.0;
	shift_hist(vn, v->m_vn_5, 2);

	// 13. Apply the final fixed filter
	vn = apply_filter(v->m_vn_5, v->m_vn_6, v->m_fx_a, 1, v->m_fx_b, 2);
	shift_hist(vn, v->m_vn_6, 2);

	return vn*0.35;
}

/*
  Playing with analog filters, or where all the magic filter formulas are coming from.

  First you start with an analog circuit, for instance this one:

  |                     +--[R2]--+
  |                     |        |
  |                     +--|C2|--+<V1     +--|C3|--+
  |                     |        |        |        |
  |  Vi   +--[R1]--+    |  |\    |        |  |\    |
  |  -----+        +----+--+-\   |        +--+-\   |
  |       +--|C1|--+       |  >--+--[Rx]--+  |  >--+----- Vo
  |                |     0-++/             0-++/   |
  |                |       |/    +--[R0]--+  |/    |
  |                |             |        |        |
  |                |             |    /|  |        |
  |                |             |   /-+--+--[R0]--+
  |                +--[R4]-------+--<  |
  |                            V2^   \++-0
  |                                   \|

  It happens to be what most of the filters in the sc01a look like.

  You need to determine the transfer function H(s) of the circuit, which is
  defined as the ratio Vo/Vi.  To do that, you use some properties:

  - The intensity through an element is equal to the voltage
    difference through the element divided by the impedance

  - The impedance of a resistance is equal to its resistance

  - The impedance of a capacitor is 1/(s*C) where C is its capacitance

  - The impedance of elements in series is the sum of their impedances

  - The impedance of elements in parallel is the inverse of the sum of
    their inverses

  - The sum of all intensities flowing into a node is 0 (there's no
    charge accumulation in a wire)

  - An operational amplifier in looped mode is an interesting beast:
    the intensity at its two inputs is always 0, and the voltage is
    forced identical between the inputs.  In our case, since the '+'
    inputs are all tied to ground, that means that the '-' inputs are at
    voltage 0, intensity 0.

  From here we can build some equations.  Noting:
  X1 = 1/(1/R1 + s*C1)
  X2 = 1/(1/R2 + s*C2)
  X3 = 1/(s*C3)

  Then computing the intensity flow at each '-' input we have:
  Vi/X1 + V2/R4 + V1/X2 = 0
  V2/R0 + Vo/R0 = 0
  V1/Rx + Vo/X3 = 0

  Wrangling the equations, one eventually gets:
  |                            1 + s * C1*R1
  | Vo/Vi = H(s) = (R4/R1) * -------------------------------------------
  |                            1 + s * C3*Rx*R4/R2 + s^2 * C2*C3*Rx*R4

  To check the mathematics between the 's' stuff, check "Laplace
  transform".  In short, it's a nice way of manipulating derivatives
  and integrals without having to manipulate derivatives and
  integrals.

  With that transfer function, we first can compute what happens to
  every frequency in the input signal.  You just compute H(2i*pi*f)
  where f is the frequency, which will give you a complex number
  representing the amplitude and phase effect.  To get the usual dB
  curves, compute 20*log10(abs(v))).

  Now, once you have an analog transfer function, you can build a
  digital filter from it using what is called the bilinear transform.

  In our case, we have an analog filter with the transfer function:
  |                 1 + k[0]*s
  |        H(s) = -------------------------
  |                 1 + k[1]*s + k[2]*s^2

  We can always reintroduce the global multiplier later, and it's 1 in
  most of our cases anyway.

  The we pose:
  |                    z-1
  |        s(z) = zc * ---
  |                    z+1

  where zc = 2*pi*fr/tan(pi*fr/fs)
  with fs = sampling frequency
  and fr = most interesting frequency

  Then we rewrite H in function of negative integer powers of z.

  Noting m0 = zc*k[0], m1 = zc*k[1], m2=zc*zc*k[2],

  a little equation wrangling then gives:

  |                 (1+m0)    + (3+m0)   *z^-1 + (3-m0)   *z^-2 +    (1-m0)*z^-3
  |        H(z) = ----------------------------------------------------------------
  |                 (1+m1+m2) + (3+m1-m2)*z^-1 + (3-m1-m2)*z^-2 + (1-m1+m2)*z^-3

  That beast in the digital transfer function, of which you can
  extract response curves by posing z = exp(2*i*pi*f/fs).

  Note that the bilinear transform is an approximation, and H(z(f)) =
  H(s(f)) only at frequency fr.  And the shape of the filter will be
  better respected around fr.  If you look at the curves of the
  filters we're interested in, the frequency:
  fr = sqrt(abs(k[0]*k[1]-k[2]))/(2*pi*k[2])

  which is a (good) approximation of the filter peak position is a
  good choice.

  Note that terminology wise, the "standard" bilinear transform is
  with fr = fs/2, and using a different fr is called "pre-warping".

  So now we have a digital transfer function of the generic form:

  |                 a[0] + a[1]*z^-1 + a[2]*z^-2 + a[3]*z^-3
  |        H(z) = --------------------------------------------
  |                 b[0] + b[1]*z^-1 + b[2]*z^-2 + b[3]*z^-3

  The magic then is that the powers of z represent time in samples.
  Noting x the input stream and y the output stream, you have:
  H(z) = y(z)/x(z)

  or in other words:
  y*b[0]*z^0 + y*b[1]*z^-1 + y*b[2]*z^-2 + y*b[3]*z^-3 = x*a[0]*z^0 + x*a[1]*z^-1 + x*a[2]*z^-2 + x*a[3]*z^-3

  i.e.

  y*z^0 = (x*a[0]*z^0 + x*a[1]*z^-1 + x*a[2]*z^-2 + x*a[3]*z^-3 - y*b[1]*z^-1 - y*b[2]*z^-2 - y*b[3]*z^-3) / b[0]

  and powers of z being time in samples,

  y[0] = (x[0]*a[0] + x[-1]*a[1] + x[-2]*a[2] + x[-3]*a[3] - y[-1]*b[1] - y[-2]*b[2] - y[-3]*b[3]) / b[0]

  So you have a filter you can apply.  Note that this is why you want
  negative powers of z.  Positive powers would mean looking into the
  future (which is possible in some cases, in particular with x, and
  has some very interesting properties, but is not very useful in
  analog circuit simulation).

  Note that if you have multiple inputs, all this stuff is linear.
  Or, in other words, you just have to split it in multiple circuits
  with only one input connected each time and sum the results.  It
  will be correct.

  Also, since we're in practice in a dynamic system, for an amplifying
  filter (i.e. where things like r4/r1 is not 1), it's better to
  proceed in two steps:

  - amplify the input by the current value of the coefficient, and
    historize it
  - apply the now non-amplifying filter to the historized amplified
    input

  That way reduces the probability of the output bouncing all over the
  place.

  Except, we're not done yet.  Doing resistors precisely in an IC is
  very hard and/or expensive (you may have heard of "laser cut
  resistors" in DACs of the time).  Doing capacitors is easier, and
  their value is proportional to their surface.  So there are no
  resistors on the sc01 die (which is a lie, there are three, but not
  in the filter path.  They are used to scale the voltage in the pitch
  wave and to generate +5V from the +9V), but a magic thing called a
  switched capacitor.  Lookup patent 4,433,210 for details.  Using
  high frequency switching a capacitor can be turned into a resistor
  of value 1/(C*f) where f is the switching frequency (20Khz,
  main/36).  And the circuit is such that the absolute value of the
  capacitors is irrelevant, only their ratio is useful, which factors
  out the intrinsic capacity-per-surface-area of the IC which may be
  hard to keep stable from one die to another.  As a result all the
  capacitor values we use are actually surfaces in square micrometers.

  For the curious, it looks like the actual capacitance was around 25
  femtofarad per square micrometer.

*/

static void build_standard_filter(votrax *v, double *a, double *b,
		double c1t, // Unswitched cap, input, top
		double c1b, // Switched cap, input, bottom
		double c2t, // Unswitched cap, over first amp-op, top
		double c2b, // Switched cap, over first amp-op, bottom
		double c3,  // Cap between the two op-amps
		double c4)  // Cap over second op-amp
{
	const double PI = M_PI;

	// First compute the three coefficients of H(s).  One can note
	// that there is as many capacitor values on both sides of the
	// division, which confirms that the capacity-per-surface-area
	// is not needed.
	double k0 = c1t / (v->m_cclock * c1b);
	double k1 = c4 * c2t / (v->m_cclock * c1b * c3);
	double k2 = c4 * c2b / (v->m_cclock * v->m_cclock * c1b * c3);

	// Estimate the filter cutoff frequency
	double fpeak = sqrt(fabs(k0*k1 - k2))/(2*PI*k2);

	// Turn that into a warp multiplier
	double zc = 2*PI*fpeak/tan(PI*fpeak / v->m_sclock);

	// Finally compute the result of the z-transform
	double m0 = zc*k0;
	double m1 = zc*k1;
	double m2 = zc*zc*k2;

	a[0] = 1+m0;
	a[1] = 3+m0;
	a[2] = 3-m0;
	a[3] = 1-m0;
	b[0] = 1+m1+m2;
	b[1] = 3+m1-m2;
	b[2] = 3-m1-m2;
	b[3] = 1-m1+m2;
}

/*
  Second filter type used once at the end, much simpler:

  |           +--[R1]--+
  |           |        |
  |           +--|C1|--+
  |           |        |
  |  Vi       |  |\    |
  |  ---[R0]--+--+-\   |
  |              |  >--+------ Vo
  |            0-++/
  |              |/


  Vi/R0 = Vo / (1/(1/R1 + s.C1)) = Vo (1/R1 + s.C1)
  H(s) = Vo/Vi = (R1/R0) * (1 / (1 + s.R1.C1))
*/

static void build_lowpass_filter(votrax *v, double *a, double *b,
		double c1t, // Unswitched cap, over amp-op, top
		double c1b) // Switched cap, over amp-op, bottom
{
	const double PI = M_PI;

	// The caps values puts the cutoff at around 150Hz, put that's no good.
	// Recordings shows we want it around 4K, so fuzz it.

	// Compute the only coefficient we care about
	double k = c1b / (v->m_cclock * c1t) * (150.0/4000.0);

	// Compute the filter cutoff frequency
	double fpeak = 1/(2*PI*k);

	// Turn that into a warp multiplier
	double zc = 2*PI*fpeak/tan(PI*fpeak / v->m_sclock);

	// Finally compute the result of the z-transform
	double m = zc*k;

	a[0] = 1;
	b[0] = 1+m;
	b[1] = 1-m;
}

/*
  Used to shape the white noise

         +-------------------------------------------------------------------+
         |                                                                   |
         +--|C1|--+---------|C3|----------+--|C4|--+                         |
         |        |      +        +       |        |                         |
   Vi    |  |\    |     (1)      (1)      |        |       +        +        |
   -|R0|-+--+-\   |      |        |       |  |\    |      (1)      (1)       |
            |  >--+--(2)-+--|C2|--+---(2)-+--+-\   |       |        |        |
          0-++/          |                   |  >--+--(2)--+--|C5|--+---(2)--+
            |/          Vo                 0-++/
                                             |/
   Equivalent:

         +------------------|R5|-------------------+
         |                                         |
         +--|C1|--+---------|C3|----------+--|C4|--+
         |        |                       |        |
   Vi    |  |\    |                       |        |
   -|R0|-+--+-\   |                       |  |\    |
            |  >--+---------|R2|----------+--+-\   |
          0-++/   |                          |  >--+
            |/   Vo                        0-++/
                                             |/

  We assume r0 = r2
*/

static void build_noise_shaper_filter(votrax *v, double *a, double *b,
		double c1,  // Cap over first amp-op
		double c2t, // Unswitched cap between amp-ops, input, top
		double c2b, // Switched cap between amp-ops, input, bottom
		double c3,  // Cap over second amp-op
		double c4)  // Switched cap after second amp-op
{
	const double PI = M_PI;

	// Coefficients of H(s) = k1*s / (1 + k2*s + k3*s^2)
	double k0 = c2t*c3*c2b/c4;
	double k1 = c2t*(v->m_cclock * c2b);
	double k2 = c1*c2t*c3/(v->m_cclock * c4);

	// Estimate the filter cutoff frequency
	double fpeak = sqrt(1/k2)/(2*PI);

	// Turn that into a warp multiplier
	double zc = 2*PI*fpeak/tan(PI*fpeak / v->m_sclock);

	// Finally compute the result of the z-transform
	double m0 = zc*k0;
	double m1 = zc*k1;
	double m2 = zc*zc*k2;

	a[0] = m0;
	a[1] = 0;
	a[2] = -m0;
	b[0] = 1+m1+m2;
	b[1] = 2-2*m2;
	b[2] = 1-m1+m2;
}

/*
  Noise injection in f2

  |                     +--[R2]--+        +--[R1]-------- Vi
  |                     |        |        |
  |                     +--|C2|--+<V1     +--|C3|--+
  |                     |        |        |        |
  |                     |  |\    |        |  |\    |
  |                +----+--+-\   |        +--+-\   |
  |                |       |  >--+--[Rx]--+  |  >--+----- Vo
  |                |     0-++/             0-++/   |
  |                |       |/    +--[R0]--+  |/    |
  |                |             |        |        |
  |                |             |    /|  |        |
  |                |             |   /-+--+--[R0]--+
  |                +--[R4]-------+--<  |
  |                            V2^   \++-0
  |                                   \|

  We drop r0/r1 out of the equation (it factorizes), and we rescale so
  that H(infinity)=1.
*/

static void build_injection_filter(votrax *v, double *a, double *b,
		double c1b, // Switched cap, input, bottom
		double c2t, // Unswitched cap, over first amp-op, top
		double c2b, // Switched cap, over first amp-op, bottom
		double c3,  // Cap between the two op-amps
		double c4)  // Cap over second op-amp
{
	// First compute the three coefficients of H(s) = (k0 + k2*s)/(k1 + k2*s)
	double k0 = v->m_cclock * c2t;
	double k1 = v->m_cclock * (c1b * c3 / c2t + c2t);
	double k2 = c2b;

	// Don't pre-warp
	double zc = 2*v->m_sclock;

	// Finally compute the result of the z-transform
	double m = zc*k2;

	(void)c4;
	a[0] = k0 + m;
	a[1] = k0 - m;
	b[0] = k1 + m;
	b[1] = k1 - m;
}

/* ---- the frame around the chip (EmuBuddy) ---- */

// MAME's phone_tick: the strobe -> commit transition, and the end of
// the phone, when A/R rises
static void phone_tick(votrax *v)
{
	switch(v->m_timer_param) {
	case T_COMMIT_PHONE:
		phone_commit(v);
		v->m_timer_left += 16*(v->m_rom_duration*4+1)*4*9+2;
		v->m_timer_param = T_END_OF_PHONE;
		break;

	case T_END_OF_PHONE:
		v->m_ar_state = 1;
		v->m_timer_param = T_NONE;
		break;
	}
}

// One sample at the chip's rate (MAME's sound_stream_update), with the
// timer moved on by the main clock ticks of that sample
static double chip_sample(votrax *v)
{
	if(v->m_timer_param != T_NONE) {
		v->m_timer_left -= 18;
		while(v->m_timer_param != T_NONE && v->m_timer_left <= 0)
			phone_tick(v);
	}
	v->m_sample_count++;
	if(v->m_sample_count & 1)
		chip_update(v);
	return analog_calc(v);
}

void votrax_reset(votrax *v)
{
	// Technically, there's no reset in this chip, and initial state
	// is random.  Still, it's a good idea to start it with something
	// sane.

	v->m_phone = 0x3f;
	v->m_inflection = 0;
	v->m_ar_state = 1;
	v->m_timer_param = T_NONE;
	v->m_timer_left = 0;

	v->m_sample_count = 0;

	// Initialize the m_rom* values
	phone_commit(v);

	// Clear the interpolation sram
	v->m_cur_fa = v->m_cur_fc = v->m_cur_va = 0;
	v->m_cur_f1 = v->m_cur_f2 = v->m_cur_f2q = v->m_cur_f3 = 0;

	// Initialize the m_filt* values and the filter coefficients
	filters_commit(v, 1);

	// Clear the rest of the internal digital state
	v->m_pitch = 0;
	v->m_closure = 0;
	v->m_update_counter = 0;
	v->m_cur_closure = 1;
	v->m_noise = 0;
	v->m_cur_noise = 0;

	// Clear the analog level histories
	memset(v->m_voice_1, 0, sizeof(v->m_voice_1));
	memset(v->m_voice_2, 0, sizeof(v->m_voice_2));
	memset(v->m_voice_3, 0, sizeof(v->m_voice_3));

	memset(v->m_noise_1, 0, sizeof(v->m_noise_1));
	memset(v->m_noise_2, 0, sizeof(v->m_noise_2));
	memset(v->m_noise_3, 0, sizeof(v->m_noise_3));
	memset(v->m_noise_4, 0, sizeof(v->m_noise_4));

	memset(v->m_vn_1, 0, sizeof(v->m_vn_1));
	memset(v->m_vn_2, 0, sizeof(v->m_vn_2));
	memset(v->m_vn_3, 0, sizeof(v->m_vn_3));
	memset(v->m_vn_4, 0, sizeof(v->m_vn_4));
	memset(v->m_vn_5, 0, sizeof(v->m_vn_5));
	memset(v->m_vn_6, 0, sizeof(v->m_vn_6));

	v->m_out_pos = 0;
	v->m_out_sum = 0;
}

votrax *votrax_new(const unsigned char *rom, double mainclock, double out_rate)
{
	votrax *v = calloc(1, sizeof(*v));

	if(!v)
		return 0;
	memcpy(v->rom, rom, VOTRAX_ROM_SIZE);
	v->m_mainclock = mainclock;
	v->m_sclock = mainclock / 18.0;
	v->m_cclock = mainclock / 36.0;
	v->m_out_rate = out_rate;
	votrax_reset(v);
	return v;
}

void votrax_free(votrax *v)
{
	free(v);
}

void votrax_write(votrax *v, int data)
{
	// only 6 bits matter
	v->m_phone = data & 0x3f;

	v->m_ar_state = 0;

	// Schedule a commit/ar reset at roughly 0.1ms in the future (one
	// phi1 transition followed by the rom extra state in practice),
	// but only if there isn't already one on the fly.  It will
	// override an end-of-phone timeout if there's one pending, but
	// that's not a problem since stb does that anyway.
	if(v->m_timer_param != T_COMMIT_PHONE) {
		v->m_timer_left = 72;
		v->m_timer_param = T_COMMIT_PHONE;
	}
}

void votrax_inflection(votrax *v, int data)
{
	// only 2 bits matter
	v->m_inflection = data & 3;
}

int votrax_ar(const votrax *v)
{
	return v->m_ar_state;
}

int votrax_phone(const votrax *v)
{
	return v->m_phone;
}

long votrax_run(votrax *v, long n, double *out, int stop_on_ar)
{
	double step = v->m_sclock / v->m_out_rate;	// chip samples per host sample
	long i;
	int ar_before;

	for(i = 0; i < n; i++) {
		int cnt = 0;

		ar_before = v->m_ar_state;
		v->m_out_sum = 0;
		// the chip samples that fall into this host sample, averaged
		while(v->m_out_pos < step) {
			v->m_out_sum += chip_sample(v);
			v->m_out_pos += 1.0;
			cnt++;
		}
		v->m_out_pos -= step;
		if(cnt)
			v->m_out_last = v->m_out_sum / cnt;
		out[i] = v->m_out_last;
		if(stop_on_ar && !ar_before && v->m_ar_state)
			return i + 1;
	}
	return n;
}
