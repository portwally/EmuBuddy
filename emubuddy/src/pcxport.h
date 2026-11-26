/* Applied Engineering PC Transporter (pcxport.c) */
#ifndef EMUBUDDY_PCXPORT_H
#define EMUBUDDY_PCXPORT_H

extern int	g_pcxport_slot;		/* config: the card's slot, 0 = none */

/* Plug the card into a slot (1, 2 or 4), or take it out (0) */
void	pcxport_attach(int slot);
/* EV_PCXPORT (sim65816.c): the V30's slice of time */
void	pcxport_event(dword64 dfcyc);
/* sim65816.c */
void	add_event_pcxport(dword64 dfcyc);

#endif
