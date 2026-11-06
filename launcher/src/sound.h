/* EmuBuddy Launcher: small interface sounds, made from simple tones (no sound files).
 * If there is no audio device everything here quietly does nothing. */
#ifndef LAUNCHER_SOUND_H
#define LAUNCHER_SOUND_H

typedef enum { SND_MOVE, SND_SELECT, SND_LAUNCH, SND_BACK, SND_MENU } Sound;

void	snd_init(void);
void	snd_shutdown(void);
void	snd_enable(int on);
int	snd_enabled(void);
void	snd_play(Sound s);

#endif
