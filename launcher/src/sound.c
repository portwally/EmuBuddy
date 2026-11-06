/* Interface sounds: a few short soft tones made in memory and played through
 * one SDL audio stream. */
#include <SDL3/SDL.h>
#include <math.h>
#include "sound.h"

#define RATE	44100

static SDL_AudioStream	*g_stream;
static int		g_on = 1;
static Sint16		*g_buf[5];
static int		g_len[5];

/* notes: frequency (Hz, 0 = rest), length in ms; a soft attack and decay on each */
typedef struct { float hz; int ms; } Note;

static void
make(Sound s, const Note *notes, int nn, float vol)
{
	int	total = 0, pos = 0;
	Sint16	*b;

	for(int i = 0; i < nn; i++) {
		total += notes[i].ms * RATE / 1000;
	}
	b = SDL_calloc((size_t)total, sizeof(Sint16));
	if(!b) {
		return;
	}
	for(int i = 0; i < nn; i++) {
		int	len = notes[i].ms * RATE / 1000;

		for(int k = 0; k < len; k++) {
			float	t = (float)k / RATE, env, v;
			float	att = SDL_min(1.0f, (float)k / (RATE * 0.004f));
			float	dec = 1.0f - (float)k / (float)len;

			env = att * dec * dec;
			v = notes[i].hz > 0 ? sinf(6.2831853f * notes[i].hz * t) +
				0.3f * sinf(6.2831853f * notes[i].hz * 2 * t) : 0;
			b[pos + k] = (Sint16)(v * env * vol * 20000.0f);
		}
		pos += len;
	}
	g_buf[s] = b;
	g_len[s] = total;
}

void
snd_init(void)
{
	SDL_AudioSpec	spec;

	if(g_stream) {
		return;
	}
	if(!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
		return;
	}
	spec.format = SDL_AUDIO_S16;
	spec.channels = 1;
	spec.freq = RATE;
	g_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
	if(!g_stream) {
		return;
	}
	{
		static const Note move[] = { { 1320, 28 } };
		static const Note sel[] = { { 660, 50 }, { 990, 90 } };
		static const Note launch[] = { { 523, 70 }, { 659, 70 }, { 784, 70 }, { 1047, 220 } };
		static const Note back[] = { { 880, 50 }, { 587, 100 } };
		static const Note menu[] = { { 990, 40 }, { 1320, 60 } };

		make(SND_MOVE, move, 1, 0.35f);
		make(SND_SELECT, sel, 2, 0.5f);
		make(SND_LAUNCH, launch, 4, 0.55f);
		make(SND_BACK, back, 2, 0.45f);
		make(SND_MENU, menu, 2, 0.4f);
	}
	SDL_ResumeAudioStreamDevice(g_stream);
}

void
snd_shutdown(void)
{
	if(g_stream) {
		SDL_DestroyAudioStream(g_stream);
		g_stream = NULL;
	}
	for(int i = 0; i < 5; i++) {
		SDL_free(g_buf[i]);
		g_buf[i] = NULL;
	}
}

void
snd_enable(int on)
{
	g_on = on != 0;
}

int
snd_enabled(void)
{
	return g_on;
}

void
snd_play(Sound s)
{
	if(!g_on || !g_stream || !g_buf[s]) {
		return;
	}
	if(SDL_GetAudioStreamQueued(g_stream) > RATE / 10 * 2) {
		SDL_ClearAudioStream(g_stream);		/* fast scrolling: don't pile up */
	}
	SDL_PutAudioStreamData(g_stream, g_buf[s], g_len[s] * (int)sizeof(Sint16));
}
