/* EmuBuddy: connects a serial (SCC) port to the virtual ImageWriter II.
 * Serial port mode 4 (g_serial_cfg[port] == 4) is "ImageWriter printer".
 * Bytes the IIgs sends go to imagewriter_loop(); a page that the program
 * doesn't finish with a form feed is ejected after a few idle seconds. */
#include "defc.h"
#include "scc.h"
#include "imagewriter.h"

extern Scc	g_scc[2];
extern word32	g_vbl_count;

/* used by imagewriter.cpp */
char	*g_imagewriter_fixed_font = "";
char	*g_imagewriter_prop_font = "";
int	iw_scc_write = 0;		/* printer wants to send its ID string */

#define IW_DPI		144
#define IW_PAPER	0		/* US Letter */
#define IW_IDLE_SECS	3

static int	g_iw_ready;
static word32	g_iw_eject_vbl;		/* eject the page at this vbl, 0 = none */

void
scc_printer_open(int port)
{
	if(!g_iw_ready) {
		imagewriter_init(IW_DPI, IW_PAPER, 0, "page", 0);
		g_iw_ready = 1;
	}
	g_scc[port].cur_state = 4;
	printf("Serial port %d: ImageWriter II printer\n", port);
}

void
scc_printer_empty_writebuf(dword64 dfcyc, int port)
{
	Scc	*scc_ptr = &(g_scc[port]);
	int	rdptr;
	(void)dfcyc;

	while((rdptr = scc_ptr->out_rdptr) != scc_ptr->out_wrptr) {
		imagewriter_loop(scc_ptr->out_buf[rdptr]);
		scc_ptr->out_rdptr = (rdptr + 1) & (SCC_OUTBUF_SIZE - 1);
		g_iw_eject_vbl = g_vbl_count + IW_IDLE_SECS * 60;
	}
}

/* The printer answers an identification request (ESC ?). */
void
scc_printer_fill_readbuf(dword64 dfcyc, int port, int space_left)
{
	static const byte id[] = { 'I', 'W', '1', '0', 'C', 'F', 0x0d };
	int	i;

	if(!iw_scc_write || space_left < (int)sizeof(id)) {
		return;
	}
	for(i = 0; i < (int)sizeof(id); i++) {
		scc_add_to_readbuf(dfcyc, port, id[i]);
	}
	iw_scc_write = 0;
}

/* Called once per frame: eject an unfinished page after a pause. */
void
printer_vbl(void)
{
	if(g_iw_eject_vbl && g_vbl_count >= g_iw_eject_vbl) {
		g_iw_eject_vbl = 0;
		imagewriter_feed();
	}
}

/* "Eject Page" from the Printer window. */
void
printer_eject_now(void)
{
	if(g_iw_ready) {
		g_iw_eject_vbl = 0;
		imagewriter_feed();
	}
}
