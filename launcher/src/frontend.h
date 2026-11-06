/* EmuBuddy Launcher front end: the picture-first "Arcade" view (a horizontal
 * row of big covers over a blurred background, like Arcadeflow). It only
 * draws and reports what the user chose; main.c owns the profiles. */
#ifndef LAUNCHER_FRONTEND_H
#define LAUNCHER_FRONTEND_H

#include <SDL3/SDL.h>
#include "profile.h"

typedef struct {
	Profile	*list;
	int	*count;
	int	*sel;
	char	*artwork_root;		/* default art folder (Launcher setting), may be "" */
	void	(*play)(void);
	void	(*overlay)(void);	/* the menu on Space */
	void	(*edit)(void);
} FeData;

int	fe_init(SDL_Renderer *r, const char *fonts_dir);
void	fe_shutdown(void);
void	fe_set_data(const FeData *d);
/* Draw the whole view at W x H logical pixels. */
void	fe_draw(int W, int H);
/* Keys and typed text; return 1 if used. */
int	fe_key(SDL_Keycode k, SDL_Keymod mod);
int	fe_text(const char *utf8);
/* A click at logical x, y (clicks: 2 = double). Returns 1 if used. */
int	fe_click(float x, float y, int clicks);
void	fe_wheel(float dy);
/* Look for a picture of a profile: kind 0 = tile (box art, snap, screen shot), 1 = logo (wheel).
 * Returns 1 and the path, or 0. */
int	fe_find_art(const Profile *p, const char *root, int kind, char *out, int outsize);
int	fe_info_open(void);
/* 1: show the controller hints (A play, X details, Y menu) instead of the key hints. */
void	fe_set_pad_hints(int on);

#endif
