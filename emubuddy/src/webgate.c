/* EmuBuddy web gateway. When the IIgs goes online, EmuBuddy starts GSGate, a
 * small Python program that fetches web pages for IIgs browsers (from HTTPS
 * sites too), reduces them to plain pages and converts pictures. It listens
 * on this computer's 127.0.0.1 only, so nothing is opened to the network; the
 * IIgs uses it as a proxy at NET_HOST_ADDR (192.168.64.254), port
 * g_webgate_port (0 = off).
 *
 * GSGate is compiled in (gateway_py.h, made by tools/gateway/embed.py) and
 * written to the application data folder at every start. For development:
 *   EMUBUDDY_GATEWAY=<path to gsgate.py>   run this copy instead
 *   EMUBUDDY_PYTHON=<path>                 the Python 3 to use
 *   EMUBUDDY_NOGATEWAY=1                   don't start it (test runs)
 * The gateway stops when EmuBuddy does, also after a crash: it watches the
 * pipe on its standard input, which closes when this process ends. */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui.h"
#include "uthernet2.h"
#include "webgate.h"
#include "gateway_py.h"

extern int	g_webgate_port;

enum { W_IDLE, W_STARTING, W_RUNNING, W_GAVE_UP };

static SDL_Process	*g_proc;
static int		g_state = W_IDLE;
static Uint64		g_started;
static char		g_status[200] = "Web gateway: starts when the IIgs goes online";
static char		g_log[1300];

static int
file_exists(const char *path)
{
	SDL_PathInfo	info;

	return SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_FILE;
}

/* A Python 3 on this computer, or NULL. /usr/bin/python3 is left out on
 * macOS on purpose: without Apple's developer tools it only opens a dialog
 * that offers to install them. */
static const char *
find_python(void)
{
	static const char *cand[] = {
#if defined(_WIN32)
		NULL
#elif defined(__APPLE__)
		"/opt/homebrew/bin/python3", "/usr/local/bin/python3",
		"/Library/Frameworks/Python.framework/Versions/Current/bin/python3",
		"/Library/Developer/CommandLineTools/usr/bin/python3",
		"/Applications/Xcode.app/Contents/Developer/usr/bin/python3", NULL
#else
		"/usr/bin/python3", "/usr/local/bin/python3", "/bin/python3", NULL
#endif
	};
	const char	*e = getenv("EMUBUDDY_PYTHON");
	int		i;

	if(e && e[0]) {
		return e;
	}
	for(i = 0; cand[i]; i++) {
		if(file_exists(cand[i])) {
			return cand[i];
		}
	}
#ifdef _WIN32
	return "python";		/* through the PATH, when it is installed */
#else
	return NULL;
#endif
}

static int
put_file(const char *dir, const char *name, const unsigned char *data, size_t len,
							char *out, size_t outsz)
{
	SDL_IOStream	*io;
	size_t		n;

	snprintf(out, outsz, "%s%s", dir, name);
	io = SDL_IOFromFile(out, "wb");
	if(!io) {
		return -1;
	}
	n = SDL_WriteIO(io, data, len);
	return (SDL_CloseIO(io) && n == len) ? 0 : -1;
}

static void
give_up(const char *fmt, const char *arg)
{
	g_state = W_GAVE_UP;
	snprintf(g_status, sizeof(g_status), fmt, arg);
	printf("%s\n", g_status);
	ui_message("%s", g_status);
}

static void
start(void)
{
	char		script[1300], pic[1300], port[16];
	const char	*py = find_python(), *over = getenv("EMUBUDDY_GATEWAY");
	const char	*args[14];
	char		*pref;
	int		n = 0;
	SDL_PropertiesID props;

	if(!py) {
		give_up("Web gateway off: no Python 3 found on this computer%s", "");
		return;
	}
	pref = SDL_GetPrefPath("EmuBuddy", "Gateway");
	if(!pref) {
		give_up("Web gateway off: no folder for its files (%s)", SDL_GetError());
		return;
	}
	/* one log per port: two EmuBuddys with different ports don't share a file */
	snprintf(g_log, sizeof(g_log), "%sgsgate-%d.log", pref, g_webgate_port);
	if(over && over[0]) {
		snprintf(script, sizeof(script), "%s", over);
	} else if(put_file(pref, "gsgate.py", gsgate_py, sizeof(gsgate_py), script, sizeof(script)) ||
			put_file(pref, "gspic.py", gspic_py, sizeof(gspic_py), pic, sizeof(pic))) {
		give_up("Web gateway off: can't write its files to %s", pref);
		SDL_free(pref);
		return;
	}
	SDL_free(pref);
	snprintf(port, sizeof(port), "%d", g_webgate_port);
	args[n++] = py;
	args[n++] = script;
	args[n++] = "--bind";
	args[n++] = "127.0.0.1";
	args[n++] = "--port";
	args[n++] = port;
	args[n++] = "--alias";
	args[n++] = NET_HOST_ADDR;
	args[n++] = "--watch-stdin";
	args[n++] = "--log";
	args[n++] = g_log;
	args[n] = NULL;
	props = SDL_CreateProperties();
	SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void *)args);
	/* our end of its standard input stays open as long as we live */
	SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_APP);
	SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_NULL);
	SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
	g_proc = SDL_CreateProcessWithProperties(props);
	SDL_DestroyProperties(props);
	if(!g_proc) {
		give_up("Web gateway off: could not start Python (%s)", SDL_GetError());
		return;
	}
	g_state = W_STARTING;
	g_started = SDL_GetTicks();
	printf("Web gateway: started %s %s on port %s\n", py, script, port);
}

void
webgate_poll(int net_active)
{
	int	code = 0;

	if(g_state == W_IDLE) {
		if(net_active && g_webgate_port > 0 && !getenv("EMUBUDDY_NOGATEWAY")) {
			start();
		}
		return;
	}
	if(!g_proc) {
		return;
	}
	if(g_webgate_port <= 0) {		/* switched off in the settings */
		webgate_stop();
		snprintf(g_status, sizeof(g_status), "Web gateway: off");
		return;
	}
	if(SDL_WaitProcess(g_proc, false, &code)) {
		char	num[16];

		SDL_DestroyProcess(g_proc);
		g_proc = NULL;
		snprintf(num, sizeof(num), "%d", g_webgate_port);
		if(g_state == W_STARTING && code == 2) {
			give_up("Web gateway: port %s is in use (another EmuBuddy may run it already)", num);
		} else {
			give_up("Web gateway stopped; see %s", g_log);
		}
	} else if(g_state == W_STARTING && SDL_GetTicks() - g_started > 1500) {
		g_state = W_RUNNING;
		snprintf(g_status, sizeof(g_status), "Web gateway: proxy %s, port %d", NET_HOST_ADDR,
									g_webgate_port);
		printf("%s\n", g_status);
		ui_message("%s", g_status);
	}
}

void
webgate_stop(void)
{
	if(g_proc) {
		SDL_KillProcess(g_proc, false);
		SDL_DestroyProcess(g_proc);	/* also closes the pipe it watches */
		g_proc = NULL;
	}
	g_state = W_IDLE;
}

const char *
webgate_status(void)
{
	if(g_webgate_port <= 0) {
		return "Web gateway: off";
	}
	return g_status;
}
