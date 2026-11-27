/**********************************************************************/
/*                    EmuBuddy - Apple //gs Emulator                  */
/*                    Copyright 2026 EmuBuddy Contributors            */
/*                                                                    */
/*      This code is covered by the GNU GPL v3                        */
/*      See the file COPYING.txt or https://www.gnu.org/licenses/     */
/**********************************************************************/

// Sequential Systems' Second Sight VGA card (see docs/SECONDSIGHT_PLAN.md).
//
// The real card has a Z180 that answers the IIgs's commands; EmuBuddy answers
// them here instead (no card ROM, no Z180).  The card answers at $C0B0-$C0BF
// whatever slot it is in:
//	$C0B0	command byte (and, while arguments are expected, argument bytes)
//	$C0B1	argument and data bytes ($C0B2 too: 16-bit stores write both)
//	$C0B2	read: returned bytes (GetStatus, GetVGAReg), after a dummy read
//	$C0B8	handshake: $01 ready for bytes, $00 taken, $A5 done, $A6 error
//
// The protocol follows Sequential's own library (vgalow.asm):  command ->
// $01 -> argument bytes -> $00 -> [data: $01, bytes, $00 per 64 KB] ->
// [long commands: $A5].  A handshake value that should follow another one
// takes over once the IIgs has read the first value, so the library's
// "wait for $00, then wait for $01" loops always see both.
//
// Phase 2: every command does its work on the card's memory: 1 MB of video
// memory, 128 KB of Z180 RAM, the palette and the VGA registers.  Drawing the
// picture from them comes in phase 3.

#include "defc.h"

int	g_secondsight = 0;		// 1: the card is in the machine

#define SS_FW_VERSION	0x11		// ROM 1.1
#define SS_MON_VGA	0x10

enum {
	SS_IDLE = 0,			// no command running
	SS_ARGS,			// collecting argument bytes
	SS_DATA,			// collecting data bytes (upload, palette)
	SS_OUT,				// IIgs reads bytes from $C0B2
};

static int	g_ss_state = SS_IDLE;
static int	g_ss_cmd = -1;
static byte	g_ss_args[96];
static int	g_ss_nargs = 0;
static int	g_ss_need = 0;
static word32	g_ss_hs = 0;		// value read at $C0B8
static int	g_ss_hs_next = -1;	// value after g_ss_hs has been read
static int	g_ss_hs_reads = 0;
static word32	g_ss_data_left = 0;	// data bytes still to come
static word32	g_ss_chunk_left = 0;	// of them in this 64 KB chunk
static byte	g_ss_out[32];
static int	g_ss_out_len = 0;
static int	g_ss_out_pos = 0;
static int	g_ss_log = -1;

static word32	g_ss_mode = 0;		// last mode set (0: none yet)
static int	g_ss_emulating = 1;	// 1: showing the Apple II video
static int	g_ss_screen_on = 1;

#define SS_VRAM_SIZE	0x100000	// 1 MB video memory
#define SS_SRAM_SIZE	0x20000		// 128 KB Z180 RAM

byte	*g_ss_vram = 0;
byte	*g_ss_sram = 0;
byte	g_ss_pal[256 * 3];		// R, G, B, 8 bits each (Bt481 DAC)
int	g_ss_border = 0;
int	g_ss_shadow = 0;		// SetShadow: 0 shadow, 4 don't
int	g_ss_font = 0;			// SetTextFont
int	g_ss_dirty = 1;			// video memory/palette/mode changed

// VGA registers, written by SetVGAReg/SetUserMode and SetMode
byte	g_ss_misc = 0;			// $3C2 miscellaneous output
byte	g_ss_feature = 0;		// $3DA feature control
byte	g_ss_seq[0x100];		// $3C4/$3C5 sequencer
byte	g_ss_crtc[0x100];		// $3D4/$3D5 CRT controller
byte	g_ss_gc[0x100];			// $3CE/$3CF graphics controller
byte	g_ss_attr[0x20];		// $3C0 attribute controller
byte	g_ss_ext[0x100];		// $3DE/$3DF Oak extended registers

static word32	g_ss_dst = 0;		// where data bytes go
static int	g_ss_dst_kind = 0;	// 0 Z180 RAM, 1 video memory, 2 palette
static word32	g_ss_dst_base = 0;	// video memory: start of its 512 KB half

// Bytes per command, including the command byte (firmware documentation)
static const int g_ss_cmd_len[16] = {
	1,	// $00 GetStatus
	3,	// $01 SetMode
	11,	// $02 Upload Code/Data
	10,	// $03 Scroll Screen
	1,	// $04 Screen Off
	1,	// $05 Screen On
	4,	// $06 SetPalette (then 768 data bytes)
	5,	// $07 SetPaletteEntry
	2,	// $08 SetBorder
	6,	// $09 Run Code
	8,	// $0A Clear Screen
	2,	// $0B SetShadow
	7,	// $0C SetVGAReg
	6,	// $0D GetVGAReg
	85,	// $0E SetUserMode (84 register bytes)
	4,	// $0F SetTextFont
};

static const char *g_ss_cmd_name[16] = {
	"GetStatus", "SetMode", "Upload", "ScrollScreen", "ScreenOff",
	"ScreenOn", "SetPalette", "SetPaletteEntry", "SetBorder", "RunCode",
	"ClearScreen", "SetShadow", "SetVGAReg", "GetVGAReg", "SetUserMode",
	"SetTextFont",
};

static int
ss_logging(void)
{
	const char *e;

	if(g_ss_log < 0) {
		e = getenv("EMUBUDDY_SSLOG");
		g_ss_log = (e && e[0] && e[0] != '0') ? 1 : 0;
	}
	return g_ss_log;
}

static void
ss_set_hs(word32 val, int next)
{
	g_ss_hs = val;
	g_ss_hs_next = next;
	g_ss_hs_reads = 0;
}

static word32
ss_get24(int pos)
{
	return g_ss_args[pos] | (g_ss_args[pos + 1] << 8) |
						(g_ss_args[pos + 2] << 16);
}

// The data phase of a command: 'len' bytes in chunks of 64 KB, each one
// opened with $01 and closed with $00.
static void
ss_start_data(word32 len)
{
	g_ss_data_left = len;
	if(len == 0) {
		g_ss_state = SS_IDLE;
		ss_set_hs(0x00, -1);
		return;
	}
	g_ss_chunk_left = (len > 0x10000) ? 0x10000 : len;
	g_ss_state = SS_DATA;
	ss_set_hs(0x00, 0x01);		// args taken, then ready for data
}

static void
ss_alloc(void)
{
	if(!g_ss_vram) {
		g_ss_vram = calloc(1, SS_VRAM_SIZE);
		g_ss_sram = calloc(1, SS_SRAM_SIZE);
	}
}

static void
ss_data_byte(word32 val)
{
	switch(g_ss_dst_kind) {
	case 0:			// Z180 RAM (code, fonts at $F000)
		g_ss_sram[g_ss_dst & (SS_SRAM_SIZE - 1)] = val;
		g_ss_dst++;
		break;
	case 1:			// video memory: stays in its 512 KB half,
				// the card cannot switch halves in a transfer
		g_ss_vram[g_ss_dst_base + (g_ss_dst & 0x7ffff)] = val;
		g_ss_dst++;
		g_ss_dirty = 1;
		break;
	case 2:			// palette
		if(g_ss_dst < sizeof(g_ss_pal)) {
			g_ss_pal[g_ss_dst++] = val;
		}
		g_ss_dirty = 1;
		break;
	}
	g_ss_data_left--;
	g_ss_chunk_left--;
	if(g_ss_data_left == 0) {
		g_ss_state = SS_IDLE;
		ss_set_hs(0x00, -1);
	} else if(g_ss_chunk_left == 0) {
		g_ss_chunk_left = (g_ss_data_left > 0x10000) ? 0x10000 :
							g_ss_data_left;
		ss_set_hs(0x00, 0x01);
	}
}

static void
ss_status_record(void)
{
	int	n = 0;

	g_ss_out[n++] = 0;		// the library's dummy read
	g_ss_out[n++] = 'G';
	g_ss_out[n++] = 'S';
	g_ss_out[n++] = 'V';
	g_ss_out[n++] = 'G';
	g_ss_out[n++] = 'A';
	g_ss_out[n++] = 5;		// bytes that follow
	g_ss_out[n++] = SS_FW_VERSION;
	g_ss_out[n++] = g_ss_emulating ? 0 : 1;
	g_ss_out[n++] = g_ss_mode;
	g_ss_out[n++] = 1;		// 1 MB of video memory
	g_ss_out[n++] = SS_MON_VGA;
	g_ss_out_len = n;
	g_ss_out_pos = 0;
	g_ss_state = SS_OUT;
	ss_set_hs(0x01, -1);
}

static byte *
ss_vga_reg(word32 idxport, word32 idx, word32 port)
{
	// Returns the register an index/data port pair names, or 0.
	(void)idxport;
	switch(port) {
	case 0x3c5: return &g_ss_seq[idx & 0xff];
	case 0x3b5:
	case 0x3d5: return &g_ss_crtc[idx & 0xff];
	case 0x3cf: return &g_ss_gc[idx & 0xff];
	case 0x3c0:
	case 0x3c1: return &g_ss_attr[idx & 0x1f];
	case 0x3df: return &g_ss_ext[idx & 0xff];
	case 0x3c2:
	case 0x3cc: return &g_ss_misc;
	case 0x3ba:
	case 0x3da: return &g_ss_feature;
	}
	return 0;
}

// The Oak extended registers of a vga_mode_rec, in the order of vga.h
static const byte g_ss_ext_idx[20] = {
	0x02, 0x03, 0x04, 0x05, 0x06,
	0x13, 0x14, 0x15, 0x17, 0x19,
	0x20, 0x21, 0x22, 0x25,
	0x30, 0x31, 0x32, 0x33, 0x34, 0x35,
};

// SetUserMode: an 84-byte vga_mode_rec (vga.h): misc, clocking mode,
// feature control, sequencer 2-4, CRTC 0-$18, graphics 0-8, attribute
// 0-$14, 20 Oak extended registers.
static void
ss_user_mode(const byte *r)
{
	int	i, p = 0;

	g_ss_misc = r[p++];
	g_ss_seq[1] = r[p++];
	g_ss_feature = r[p++];
	for(i = 2; i <= 4; i++) {
		g_ss_seq[i] = r[p++];
	}
	for(i = 0; i < 0x19; i++) {
		g_ss_crtc[i] = r[p++];
	}
	for(i = 0; i < 9; i++) {
		g_ss_gc[i] = r[p++];
	}
	for(i = 0; i < 0x15; i++) {
		g_ss_attr[i] = r[p++];
	}
	for(i = 0; i < 20; i++) {
		g_ss_ext[g_ss_ext_idx[i]] = r[p++];
	}
	g_ss_mode = 0xff;		// VM_USER
	g_ss_dirty = 1;
}

// All argument bytes of the current command are in: run it.
static void
ss_run(void)
{
	int	i;

	if(ss_logging()) {
		printf("SecondSight: %s", g_ss_cmd_name[g_ss_cmd]);
		for(i = 0; i < g_ss_nargs && i < 12; i++) {
			printf(" %02x", g_ss_args[i]);
		}
		printf("%s\n", (g_ss_nargs > 12) ? " ..." : "");
	}
	word32	src, dst, len, w;
	byte	*reg;

	g_ss_state = SS_IDLE;
	ss_alloc();
	switch(g_ss_cmd) {
	case 0x01:		// SetMode: mode, emulation flag
		g_ss_mode = g_ss_args[0];
		g_ss_emulating = (g_ss_args[1] == 0);
		g_ss_dirty = 1;
		ss_set_hs(0x00, 0xa5);
		break;
	case 0x02:		// Upload: flag, dst(3), len(3), IIgs src(3)
		dst = ss_get24(1);
		if(g_ss_args[0] & 1) {
			g_ss_dst_kind = 1;
			g_ss_dst_base = (dst >= 0x80000) ? 0x80000 : 0;
		} else {
			g_ss_dst_kind = 0;
		}
		g_ss_dst = dst;
		ss_start_data(ss_get24(4));
		break;
	case 0x03:		// ScrollScreen: src(3), dst(3), len(3);
				// a copy in the low 512 KB of video memory
		src = ss_get24(0) & 0x7ffff;
		dst = ss_get24(3) & 0x7ffff;
		len = ss_get24(6);
		if(len > 0x80000) {
			len = 0x80000;
		}
		for(w = 0; w < len; w++) {	// as the Z180's DMA: forwards
			g_ss_vram[(dst + w) & 0x7ffff] =
					g_ss_vram[(src + w) & 0x7ffff];
		}
		g_ss_dirty = 1;
		ss_set_hs(0x00, 0xa5);
		break;
	case 0x06:		// SetPalette: IIgs address(3), then 768 bytes
		g_ss_dst_kind = 2;
		g_ss_dst = 0;
		ss_start_data(768);
		break;
	case 0x07:		// SetPaletteEntry: entry, R, G, B
		w = g_ss_args[0];
		g_ss_pal[w * 3] = g_ss_args[1];
		g_ss_pal[w * 3 + 1] = g_ss_args[2];
		g_ss_pal[w * 3 + 2] = g_ss_args[3];
		g_ss_dirty = 1;
		ss_set_hs(0x00, -1);
		break;
	case 0x08:		// SetBorder: palette entry
		g_ss_border = g_ss_args[0];
		g_ss_dirty = 1;
		ss_set_hs(0x00, -1);
		break;
	case 0x0a:		// ClearScreen: colour, address(3), length(3)
		dst = ss_get24(1) & 0x7ffff;
		len = ss_get24(4);
		if(len > 0x80000) {
			len = 0x80000;
		}
		for(w = 0; w < len; w++) {
			g_ss_vram[(dst + w) & 0x7ffff] = g_ss_args[0];
		}
		g_ss_dirty = 1;
		ss_set_hs(0x00, 0xa5);
		break;
	case 0x0b:		// SetShadow
		g_ss_shadow = g_ss_args[0];
		ss_set_hs(0x00, -1);
		break;
	case 0x0c:		// SetVGAReg: idx port(2), index, port(2), value
		reg = ss_vga_reg(g_ss_args[0] | (g_ss_args[1] << 8),
			g_ss_args[2], g_ss_args[3] | (g_ss_args[4] << 8));
		if(reg) {
			*reg = g_ss_args[5];
			g_ss_dirty = 1;
		} else if(ss_logging()) {
			printf("SecondSight: SetVGAReg to unknown port\n");
		}
		ss_set_hs(0x00, -1);
		break;
	case 0x0e:		// SetUserMode
		ss_user_mode(&g_ss_args[0]);
		ss_set_hs(0x00, -1);
		break;
	case 0x0f:		// SetTextFont
		g_ss_font = g_ss_args[0];
		g_ss_dirty = 1;
		ss_set_hs(0x00, -1);
		break;
	case 0x09:		// RunCode: there is no Z180 here
		printf("SecondSight: RunCode is not supported (no Z180)\n");
		ss_set_hs(0x00, 0xa6);
		break;
	case 0x0d:		// GetVGAReg: $00, then $01 and dummy + value
		reg = ss_vga_reg(g_ss_args[0] | (g_ss_args[1] << 8),
			g_ss_args[2], g_ss_args[3] | (g_ss_args[4] << 8));
		g_ss_out[0] = 0;
		g_ss_out[1] = reg ? *reg : 0;
		g_ss_out_len = 2;
		g_ss_out_pos = 0;
		g_ss_state = SS_OUT;
		ss_set_hs(0x00, 0x01);
		break;
	default:		// short commands: arguments taken
		ss_set_hs(0x00, -1);
		break;
	}
}

static void
ss_command(word32 val)
{
	val = val & 0xff;
	if(val > 0x0f) {
		if(ss_logging()) {
			printf("SecondSight: unknown command %02x\n", val);
		}
		g_ss_state = SS_IDLE;
		ss_set_hs(0xa6, -1);
		return;
	}
	g_ss_cmd = val;
	g_ss_nargs = 0;
	g_ss_need = g_ss_cmd_len[val] - 1;
	switch(val) {
	case 0x00:
		if(ss_logging()) {
			printf("SecondSight: GetStatus\n");
		}
		ss_status_record();
		return;
	case 0x04:
		g_ss_screen_on = 0;
		g_ss_dirty = 1;
		break;
	case 0x05:
		g_ss_screen_on = 1;
		g_ss_dirty = 1;
		break;
	}
	if(g_ss_need == 0) {
		if(ss_logging()) {
			printf("SecondSight: %s\n", g_ss_cmd_name[val]);
		}
		g_ss_state = SS_IDLE;
		ss_set_hs(0x00, -1);
		return;
	}
	g_ss_state = SS_ARGS;
	ss_set_hs(0x01, -1);
}

static void
ss_byte_in(word32 val)
{
	if(g_ss_state == SS_ARGS) {
		g_ss_args[g_ss_nargs++] = val & 0xff;
		if(g_ss_nargs >= g_ss_need) {
			ss_run();
		}
	} else if(g_ss_state == SS_DATA && g_ss_hs == 0x01) {
		ss_data_byte(val & 0xff);
	} else if(ss_logging()) {
		printf("SecondSight: stray byte %02x\n", val & 0xff);
	}
}

word32
secondsight_devsel_read(word32 loc, dword64 dfcyc)
{
	word32	val;

	(void)dfcyc;
	switch(loc & 0xf) {
	case 0x2:		// $C0B2: returned bytes
		if(g_ss_state == SS_OUT && g_ss_hs == 0x01 &&
					g_ss_out_pos < g_ss_out_len) {
			val = g_ss_out[g_ss_out_pos++];
			if(g_ss_out_pos >= g_ss_out_len) {
				g_ss_state = SS_IDLE;
				ss_set_hs(0x00, -1);
			}
			return val;
		}
		return 0;
	case 0x8:		// $C0B8: handshake
		val = g_ss_hs;
		if(g_ss_hs_next >= 0) {
			g_ss_hs_reads++;
			if(g_ss_hs_reads >= 2) {
				ss_set_hs(g_ss_hs_next, -1);
			}
		}
		return val;
	}
	return 0;
}

void
secondsight_devsel_write(word32 loc, word32 val, dword64 dfcyc)
{
	(void)dfcyc;
	switch(loc & 0xf) {
	case 0x0:		// $C0B0: a command, or an argument byte
		if(g_ss_state == SS_ARGS) {
			ss_byte_in(val);
		} else {
			ss_command(val);
		}
		return;
	case 0x1:		// $C0B1 and $C0B2: argument or data bytes
	case 0x2:
		ss_byte_in(val);
		return;
	}
}

void
secondsight_reset(void)
{
	g_ss_state = SS_IDLE;
	g_ss_cmd = -1;
	g_ss_nargs = 0;
	g_ss_data_left = 0;
	g_ss_out_len = 0;
	g_ss_out_pos = 0;
	ss_set_hs(0x00, -1);
	g_ss_mode = 0;
	g_ss_emulating = 1;
	g_ss_screen_on = 1;
	g_ss_shadow = 0;
	g_ss_dirty = 1;
}

// ---- The picture (phase 3) ----

typedef struct {
	byte	mode;
	short	w, h;
	byte	bpp;		// 8 (palette), 16 (5-5-5), 24
} SsMode;

// Built-in modes (SS.Modes, the library's mode list and vga.h).  $5B, $5C
// and $70 come from the library as SetUserMode + SetMode $FF; they are here
// too in case a program sets them by number.
static const SsMode g_ss_modes[] = {
	{ 0x13, 320, 200, 8 },
	{ 0x53, 640, 480, 8 },
	{ 0x61, 640, 400, 8 },
	{ 0x54, 800, 600, 8 },
	{ 0x56, 1024, 768, 8 },
	{ 0x59, 1024, 768, 8 },
	{ 0x5a, 640, 480, 16 },
	{ 0x5b, 640, 400, 16 },
	{ 0x5c, 640, 480, 16 },
	{ 0x70, 320, 200, 16 },
	{ 0x60, 800, 600, 16 },
	{ 0x5f, 640, 480, 24 },
};

static word32	*g_ss_pix = 0;		// ARGB picture
static int	g_ss_pix_w = 0, g_ss_pix_h = 0;

// Width, height, bits per pixel, bytes per line and start address of the
// current VGA picture.  0 if the card shows no VGA picture of its own.
static int
ss_geometry(int *w, int *h, int *bpp, word32 *pitch, word32 *start)
{
	int	i, vde, dbl;

	if(!g_ss_vram || g_ss_emulating || g_ss_mode == 0) {
		return 0;
	}
	// CRTC start address (registers $0C/$0D), in units of 4 bytes as in
	// the VGA's 256-colour modes.  Not checked against a real card.
	*start = (((word32)g_ss_crtc[0x0c] << 8) | g_ss_crtc[0x0d]) * 4;
	if(g_ss_mode == 0xff) {
		// A user mode: all the library's tables (5B, 5C, 70) are 32K
		// colours, with the CRTC offset register in units of 16 bytes
		*bpp = 16;
		*pitch = g_ss_crtc[0x13] * 16;
		*w = *pitch / 2;
		vde = g_ss_crtc[0x12] | ((g_ss_crtc[0x07] & 0x02) << 7) |
					((g_ss_crtc[0x07] & 0x40) << 3);
		vde++;
		dbl = (g_ss_crtc[0x09] & 0x80) ? 2 : (g_ss_crtc[0x09] & 0x1f) + 1;
		*h = vde / dbl;
		if(*w < 64 || *h < 64 || *w > 1024 || *h > 768) {
			return 0;
		}
		return 1;
	}
	for(i = 0; i < (int)(sizeof(g_ss_modes) / sizeof(g_ss_modes[0])); i++) {
		if(g_ss_modes[i].mode == g_ss_mode) {
			*w = g_ss_modes[i].w;
			*h = g_ss_modes[i].h;
			*bpp = g_ss_modes[i].bpp;
			*pitch = *w * (*bpp / 8);
			return 1;
		}
	}
	return 0;		// text modes come in phase 4
}

static word32
ss_pal_argb(int i)
{
	return 0xff000000 | (g_ss_pal[i * 3] << 16) |
				(g_ss_pal[i * 3 + 1] << 8) | g_ss_pal[i * 3 + 2];
}

// The card's picture for the window: 1 and an ARGB picture if the card
// shows a VGA mode, 0 if the window shows the IIgs's own picture.
// *changed is 1 when the pixels are new since the last call.
int
secondsight_picture(word32 **pix, int *w, int *h, word32 *border,
								int *changed)
{
	int	pw, ph, bpp, x, y;
	word32	pitch, start, a, v, r, g, b;
	word32	*out;
	byte	*vram;

	*changed = 0;
	if(!g_secondsight || !ss_geometry(&pw, &ph, &bpp, &pitch, &start)) {
		return 0;
	}
	if(pw != g_ss_pix_w || ph != g_ss_pix_h || !g_ss_pix) {
		free(g_ss_pix);
		g_ss_pix = calloc((size_t)pw * ph, sizeof(word32));
		g_ss_pix_w = pw;
		g_ss_pix_h = ph;
		g_ss_dirty = 1;
	}
	*pix = g_ss_pix;
	*w = pw;
	*h = ph;
	*border = g_ss_screen_on ? ss_pal_argb(g_ss_border) : 0xff000000;
	if(!g_ss_dirty) {
		return 1;
	}
	g_ss_dirty = 0;
	*changed = 1;
	vram = g_ss_vram;
	for(y = 0; y < ph; y++) {
		out = g_ss_pix + (size_t)y * pw;
		a = start + y * pitch;
		if(!g_ss_screen_on) {
			for(x = 0; x < pw; x++) {
				out[x] = 0xff000000;
			}
			continue;
		}
		switch(bpp) {
		case 8:
			for(x = 0; x < pw; x++) {
				out[x] = ss_pal_argb(vram[(a + x) & 0xfffff]);
			}
			break;
		case 16:	// 5-5-5, low byte first
			for(x = 0; x < pw; x++, a += 2) {
				v = vram[a & 0xfffff] | (vram[(a + 1) & 0xfffff] << 8);
				r = (v >> 10) & 0x1f;
				g = (v >> 5) & 0x1f;
				b = v & 0x1f;
				out[x] = 0xff000000 | (((r << 3) | (r >> 2)) << 16) |
					(((g << 3) | (g >> 2)) << 8) |
					((b << 3) | (b >> 2));
			}
			break;
		case 24:	// B, G, R as on PC VGA cards; not checked
			for(x = 0; x < pw; x++, a += 3) {
				out[x] = 0xff000000 |
					(vram[(a + 2) & 0xfffff] << 16) |
					(vram[(a + 1) & 0xfffff] << 8) |
					vram[a & 0xfffff];
			}
			break;
		}
	}
	return 1;
}

// Test hook (EMUBUDDY_DUMPRAM): video memory, Z180 RAM, palette, registers
void
secondsight_dump(const char *prefix)
{
	char	fn[1100];
	FILE	*f;

	if(!g_secondsight || !g_ss_vram) {
		return;
	}
	snprintf(fn, sizeof(fn), "%s.ssvram", prefix);
	if((f = fopen(fn, "wb"))) {
		fwrite(g_ss_vram, 1, SS_VRAM_SIZE, f);
		fclose(f);
	}
	snprintf(fn, sizeof(fn), "%s.sssram", prefix);
	if((f = fopen(fn, "wb"))) {
		fwrite(g_ss_sram, 1, SS_SRAM_SIZE, f);
		fclose(f);
	}
	snprintf(fn, sizeof(fn), "%s.ssstate", prefix);
	if((f = fopen(fn, "wb"))) {
		fwrite(g_ss_pal, 1, sizeof(g_ss_pal), f);
		fputc(g_ss_mode, f);
		fputc(g_ss_emulating, f);
		fputc(g_ss_screen_on, f);
		fputc(g_ss_border, f);
		fputc(g_ss_misc, f);
		fwrite(g_ss_seq, 1, sizeof(g_ss_seq), f);
		fwrite(g_ss_crtc, 1, sizeof(g_ss_crtc), f);
		fwrite(g_ss_gc, 1, sizeof(g_ss_gc), f);
		fwrite(g_ss_attr, 1, sizeof(g_ss_attr), f);
		fwrite(g_ss_ext, 1, sizeof(g_ss_ext), f);
		fclose(f);
	}
	printf("SecondSight dumped to %s.ssvram/.sssram/.ssstate\n", prefix);
}
