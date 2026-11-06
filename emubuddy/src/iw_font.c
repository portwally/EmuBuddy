/* The Apple IIgs text font (from KEGS), used by the virtual ImageWriter to
 * print text as dot-matrix characters. See iw_shim.h. */
const unsigned char g_iw_font[256][8] = {
#include "kegsfont.h"
};
