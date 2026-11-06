/* EmuBuddy network backend used when libslirp isn't available at build time:
 * the Uthernet II card works, but nothing goes out on the network. */
#include "uthernet2.h"

int
net_init(void)
{
	return -1;
}

void
net_send(const uint8_t *frame, int len)
{
	(void)frame;
	(void)len;
}

void
net_poll(void)
{
}

void
net_shutdown(void)
{
}

const char *
net_backend_name(void)
{
	return "none (built without libslirp)";
}
