/* localtalk.h: AppleTalk for the IIgs's serial ports (see localtalk.c) */
#ifndef EMUBUDDY_LOCALTALK_H
#define EMUBUDDY_LOCALTALK_H

extern int	g_appletalk;		/* 0 = off, 1 = LocalTalk over UDP */

void		localtalk_port_on(int port);
void		localtalk_port_off(int port);
void		localtalk_shutdown(void);
void		localtalk_set_node(int node);
void		localtalk_from_gs(dword64 dfcyc, const byte *frame, int len);
int		localtalk_to_gs(dword64 dfcyc, byte *buf, int max);
int		localtalk_port(void);
const char	*localtalk_status(void);
const char	*localtalk_take_message(void);

#endif
