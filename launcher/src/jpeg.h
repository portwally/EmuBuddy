/* EmuBuddy Launcher: a small JPEG reader for cover pictures (SDL loads PNG and
 * BMP only). Baseline and progressive JPEG, 8 bit, grey or colour. No SDL
 * here, so it is easy to test. */
#ifndef LAUNCHER_JPEG_H
#define LAUNCHER_JPEG_H

#include <stddef.h>

/* Decodes a JPEG file held in memory. Returns the picture as RGBA bytes
 * (4 per pixel, top row first; free() it) and its size, or NULL if the data
 * is not a JPEG this reader understands. */
unsigned char	*jpeg_decode(const unsigned char *data, size_t len, int *w, int *h);

/* 1 if the data starts like a JPEG file. */
int		jpeg_is(const unsigned char *data, size_t len);

#endif
