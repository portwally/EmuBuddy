/* EmuBuddy web gateway: see webgate.c. */
#ifndef EMUBUDDY_WEBGATE_H
#define EMUBUDDY_WEBGATE_H

/* Call once per frame. net_active: the IIgs has the network card open. Starts
 * the gateway the first time the IIgs goes online, and notices when it stops. */
void	webgate_poll(int net_active);
/* Stop the gateway (EmuBuddy quits). */
void	webgate_stop(void);
/* One line for the user about the gateway. */
const char *webgate_status(void);

#endif
