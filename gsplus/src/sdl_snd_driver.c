/**********************************************************************/
/*                    GSplus - Apple //gs Emulator                    */
/*                    Based on KEGS by Kent Dickey                    */
/*      This code is covered by the GNU GPL v3                        */
/**********************************************************************/

/* SDL3 audio backend.
 *
 * The core PUSHES audio: each frame sound_driver.c's child_sound_playit() ->
 * reliable_buf_write() -> child_send_samples() hands us interleaved 16-bit
 * stereo samples (4 bytes per frame) at g_preferred_rate. We feed those straight
 * into an SDL3 SDL_AudioStream, which buffers (and resamples if needed) and
 * plays them -- so we don't need the manual ring buffer the old CoreAudio /
 * SDL2 backends maintained.
 */

#include <SDL3/SDL.h>

#include "defc.h"
#include "protos_sdl.h"

extern int g_preferred_rate;		/* desired sample rate, set by the core */

static SDL_AudioStream *g_sdl_audio_stream = NULL;

void
sdl_snd_init(word32 *shmaddr)
{
	SDL_AudioSpec spec;

	(void)shmaddr;			/* push model: we get samples via sdl_send_audio */

	if(!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
		printf("SDL_InitSubSystem(AUDIO) failed: %s\n", SDL_GetError());
		sound_set_audio_rate(g_preferred_rate);
		return;
	}

	spec.format = SDL_AUDIO_S16;	/* native-endian 16-bit, matches the core */
	spec.channels = 2;
	spec.freq = g_preferred_rate;

	/* NULL callback => we drive it by pushing data with SDL_PutAudioStreamData. */
	g_sdl_audio_stream = SDL_OpenAudioDeviceStream(
				SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
	if(!g_sdl_audio_stream) {
		printf("SDL_OpenAudioDeviceStream failed: %s\n", SDL_GetError());
		sound_set_audio_rate(g_preferred_rate);
		return;
	}

	/* Streams open paused; start playback. */
	SDL_ResumeAudioStreamDevice(g_sdl_audio_stream);
	sound_set_audio_rate(g_preferred_rate);
	printf("sdl_snd_init: SDL3 audio stream open, rate=%d\n", g_preferred_rate);
}

int
sdl_send_audio(byte *ptr, int size)
{
	int	max_queued;

	if(g_sdl_audio_stream) {
		/* Keep latency bounded: if the emulator runs ahead of real time
		 * (e.g. fast-forward) the queue grows without limit, so drop the
		 * backlog once it exceeds ~0.5s of stereo S16 (rate*4 bytes/sec). */
		max_queued = g_preferred_rate * 2;
		if(SDL_GetAudioStreamQueued(g_sdl_audio_stream) > max_queued) {
			SDL_ClearAudioStream(g_sdl_audio_stream);
		}
		SDL_PutAudioStreamData(g_sdl_audio_stream, ptr, size);
	}

	/* MUST be >= 0: the core's reliable_buf_write() calls exit(1) otherwise. */
	return size;
}

void
sdl_snd_shutdown(void)
{
	if(g_sdl_audio_stream) {
		SDL_DestroyAudioStream(g_sdl_audio_stream);
		g_sdl_audio_stream = NULL;
	}
}
