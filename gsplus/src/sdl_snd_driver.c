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

/* Push `bytes` of silence into the stream, to rebuild a latency cushion when
 * we're about to underrun. Done in chunks so we don't need a huge buffer. */
static void
sdl_put_silence(int bytes)
{
	byte	zerobuf[4096];
	int	chunk;

	memset(zerobuf, 0, sizeof(zerobuf));
	while(bytes > 0) {
		chunk = bytes;
		if(chunk > (int)sizeof(zerobuf)) {
			chunk = (int)sizeof(zerobuf);
		}
		SDL_PutAudioStreamData(g_sdl_audio_stream, zerobuf, chunk);
		bytes -= chunk;
	}
}

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
	int	bytes_per_sec, target, low, high, queued, pad;

	if(g_sdl_audio_stream) {
		/* SDL's audio thread drains this queue at a steady rate on its own
		 * thread, decoupled from our main-loop pushes. The emulator, however,
		 * feeds us in uneven bursts: run_16ms() paces to realtime with
		 * micro_sleep(), whose OS granularity is coarse (Windows Sleep() is
		 * ~15ms). So the *average* feed rate is correct but it arrives jittery,
		 * and if the queue ever empties between bursts, playback stutters --
		 * which is exactly what's heard on Windows. (stereo S16 = 4 bytes/frame) */
		bytes_per_sec = g_preferred_rate * 4;
		target = bytes_per_sec / 8;	/* ~125ms cushion: swamps ~15ms jitter */
		low    = bytes_per_sec / 16;	/* ~62ms: about to run dry */
		high   = bytes_per_sec / 2;	/* ~500ms: too far ahead, cap latency */

		queued = (int)SDL_GetAudioStreamQueued(g_sdl_audio_stream);

		/* About to underrun: prepend silence to rebuild the cushion. This adds
		 * no audible gap -- the queue was already near-empty so a gap was
		 * coming regardless -- and because the producer is realtime-locked the
		 * cushion then holds steady instead of being whittled back to zero. */
		if(queued < low) {
			pad = target - queued;
			if(pad > 0) {
				sdl_put_silence(pad);
			}
		}

		/* Don't let latency grow without bound when we're running ahead: drop
		 * these new samples rather than SDL_ClearAudioStream()ing the whole
		 * backlog (clearing empties the buffer and causes a gap). */
		if(queued <= high) {
			SDL_PutAudioStreamData(g_sdl_audio_stream, ptr, size);
		}
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
