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
// Phase 1: commands are framed and answered; GetStatus returns the status
// record and SetMode is remembered.  Drawing comes in later phases.

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
ss_data_byte(word32 val)
{
	// Phase 1: data is counted and dropped (video memory comes in phase 2)
	(void)val;
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
	g_ss_state = SS_IDLE;
	switch(g_ss_cmd) {
	case 0x01:		// SetMode: mode, emulation flag
		g_ss_mode = g_ss_args[0];
		g_ss_emulating = (g_ss_args[1] == 0);
		ss_set_hs(0x00, 0xa5);
		break;
	case 0x02:		// Upload: flag, dst(3), len(3), IIgs src(3)
		ss_start_data(ss_get24(4));
		break;
	case 0x06:		// SetPalette: IIgs address(3), then 768 bytes
		ss_start_data(768);
		break;
	case 0x03:		// ScrollScreen
	case 0x0a:		// ClearScreen
		ss_set_hs(0x00, 0xa5);
		break;
	case 0x09:		// RunCode: there is no Z180 here
		printf("SecondSight: RunCode is not supported (no Z180)\n");
		ss_set_hs(0x00, 0xa6);
		break;
	case 0x0d:		// GetVGAReg: $00, then $01 and dummy + value
		g_ss_out[0] = 0;
		g_ss_out[1] = 0;
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
		break;
	case 0x05:
		g_ss_screen_on = 1;
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
}
