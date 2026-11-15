/* The part of a path after its last separator: "/" everywhere, and "\" as
 * well on Windows, where the system hands out paths with backslashes. */
#ifndef EMUBUDDY_PATHUTIL_H
#define EMUBUDDY_PATHUTIL_H

#include <string.h>

static inline const char *
path_base(const char *path)
{
	const char	*p = strrchr(path, '/');
#ifdef _WIN32
	const char	*b = strrchr(path, '\\');

	if(b && (!p || b > p)) {
		p = b;
	}
#endif
	return p ? p + 1 : path;
}

#endif
