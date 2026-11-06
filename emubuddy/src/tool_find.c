/* EmuBuddy: find an external program. See tool_find.h. */
#include "tool_find.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int
is_file(const char *p)
{
	SDL_PathInfo	info;

	return SDL_GetPathInfo(p, &info) && info.type == SDL_PATHTYPE_FILE;
}

static char *
join_try(const char *dir, size_t n, const char *name)
{
	char	buf[1100];

	snprintf(buf, sizeof(buf), "%.*s/%s", (int)n, dir, name);
	return is_file(buf) ? strdup(buf) : NULL;
}

char *
tool_find(const char *env_var, const char *const *names, const char *win_dir,
						const char *win_glob)
{
	static const char *dirs[] = { "/opt/homebrew/bin", "/usr/local/bin",
				"/opt/local/bin", "/usr/bin", "/snap/bin", NULL };
	const char	*env, *path, *p, *q;
	char		*found;
	int		i, j;

	env = env_var ? getenv(env_var) : NULL;
	if(env && !strcmp(env, "none")) {
		return NULL;
	}
	if(env && is_file(env)) {
		return strdup(env);
	}
	path = getenv("PATH");
	for(p = path; p && *p; p = *q ? q + 1 : q) {
#ifdef _WIN32
		q = strchr(p, ';');
#else
		q = strchr(p, ':');
#endif
		if(!q) {
			q = p + strlen(p);
		}
		for(j = 0; names[j]; j++) {
			if((found = join_try(p, (size_t)(q - p), names[j]))) {
				return found;
			}
		}
	}
	for(i = 0; dirs[i]; i++) {
		for(j = 0; names[j]; j++) {
			if((found = join_try(dirs[i], strlen(dirs[i]), names[j]))) {
				return found;
			}
		}
	}
#ifdef _WIN32
	if(win_dir && win_glob) {
		int	n = 0;
		char	**list = SDL_GlobDirectory(win_dir, win_glob,
						SDL_GLOB_CASEINSENSITIVE, &n);

		if(list && n > 0) {
			char	buf[1100];

			snprintf(buf, sizeof(buf), "%s/%s", win_dir, list[n - 1]);
			SDL_free(list);
			return strdup(buf);
		}
		SDL_free(list);
	}
#else
	(void)win_dir;
	(void)win_glob;
#endif
	return NULL;
}
