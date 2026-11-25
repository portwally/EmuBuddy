/* EmuBuddy starts FujiNet-PC itself, and stops it when EmuBuddy ends.
 *
 * FujiNet-PC is the FujiNet as a program (docs/FUJINET_PLAN.md). When the
 * setting "FujiNet-PC program" names its folder (or the program "fujinet" in
 * it) and FujiNet is on, EmuBuddy runs it there, with its web page open to
 * this computer only. Without the setting, the user starts FujiNet-PC. */

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>
#include "ui.h"

extern int	g_fujinet;		/* smartport.c */
char		*g_fujinet_program = "";	/* config: folder or program */

static SDL_Process *g_fn_proc;
static int	g_fn_failed;		/* tried with this setting: not again */
static char	g_fn_tried[1024];

void
fujinet_run_stop(void)
{
	if(g_fn_proc) {
		SDL_KillProcess(g_fn_proc, false);
		SDL_DestroyProcess(g_fn_proc);
		g_fn_proc = NULL;
		printf("fujinet: FujiNet-PC stopped\n");
	}
}

/* 1 while a FujiNet-PC that EmuBuddy started runs */
int
fujinet_run_active(void)
{
	return g_fn_proc != NULL;
}

static void
start(void)
{
#ifdef SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING
	char		dir[1024], prog[1100];
	const char	*args[4];
	SDL_PathInfo	info;
	SDL_PropertiesID props;
	size_t		n;

	snprintf(dir, sizeof(dir), "%s", g_fujinet_program);
	n = strlen(dir);
	while(n > 1 && (dir[n - 1] == '/' || dir[n - 1] == '\\')) {
		dir[--n] = 0;
	}
	if(SDL_GetPathInfo(dir, &info) && info.type != SDL_PATHTYPE_DIRECTORY) {
		/* the program itself was named: its folder */
		char *slash = strrchr(dir, '/');
#ifdef _WIN32
		if(strrchr(dir, '\\') > slash) {
			slash = strrchr(dir, '\\');
		}
#endif
		if(slash) {
			*slash = 0;
		}
	}
#ifdef _WIN32
	snprintf(prog, sizeof(prog), "%s\\fujinet.exe", dir);
#else
	snprintf(prog, sizeof(prog), "%s/fujinet", dir);
#endif
	if(!SDL_GetPathInfo(prog, &info)) {
		g_fn_failed = 1;
		ui_message("FujiNet-PC not found in %s", dir);
		printf("fujinet: no FujiNet-PC at %s\n", prog);
		return;
	}
	args[0] = prog;
	args[1] = "-u";			/* its web page: this computer only */
	args[2] = "http://127.0.0.1:8000";
	args[3] = NULL;
	props = SDL_CreateProperties();
	SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER,
								(void *)args);
	/* it reads its settings, SD folder and web pages from its folder */
	SDL_SetStringProperty(props,
			SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING, dir);
	SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER,
							SDL_PROCESS_STDIO_NULL);
	SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER,
							SDL_PROCESS_STDIO_NULL);
	SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER,
							SDL_PROCESS_STDIO_NULL);
	g_fn_proc = SDL_CreateProcessWithProperties(props);
	SDL_DestroyProperties(props);
	if(!g_fn_proc) {
		g_fn_failed = 1;
		ui_message("Could not start FujiNet-PC");
		printf("fujinet: could not start %s: %s\n", prog, SDL_GetError());
		return;
	}
	printf("fujinet: started %s\n", prog);
#else
	g_fn_failed = 1;		/* this SDL cannot start it in its folder */
	printf("fujinet: start FujiNet-PC yourself (SDL too old to do it)\n");
#endif
}

/* Call about once a second */
void
fujinet_run_poll(void)
{
	int	code = 0;

	if(strcmp(g_fn_tried, g_fujinet_program ? g_fujinet_program : "")) {
		/* another setting: a new try */
		snprintf(g_fn_tried, sizeof(g_fn_tried), "%s",
				g_fujinet_program ? g_fujinet_program : "");
		g_fn_failed = 0;
		fujinet_run_stop();
	}
	if(g_fn_proc && SDL_WaitProcess(g_fn_proc, false, &code)) {
		SDL_DestroyProcess(g_fn_proc);	/* it ended by itself */
		g_fn_proc = NULL;
		/* FujiNet-PC asks to be started again with exit code 75 (its
		 * own run-fujinet script does that): anything else is final */
		if(code != 75) {
			g_fn_failed = 1;
			printf("fujinet: FujiNet-PC ended with code %d\n", code);
			ui_message("FujiNet-PC ended (code %d)", code);
		}
	}
	if(!g_fujinet || !g_fn_tried[0]) {
		fujinet_run_stop();
		return;
	}
	if(!g_fn_proc && !g_fn_failed) {
		start();
	}
}
