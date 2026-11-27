#include "lang.h"
#include "attract.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

static int
by_name(const void *a, const void *b)
{
	return strcasecmp((const char *)a, (const char *)b);
}

int
attract_list_emulators(const char *dir, char names[][100], int max)
{
	char		path[1300];
	DIR		*d;
	struct dirent	*e;
	int		n = 0;

	snprintf(path, sizeof(path), "%s/emulators", dir);
	d = opendir(path);
	while(d && (e = readdir(d)) != NULL && n < max) {
		size_t l = strlen(e->d_name);

		if(l > 4 && !strcasecmp(e->d_name + l - 4, ".cfg") && e->d_name[0] != '.') {
			snprintf(names[n], 100, "%.*s", (int)(l - 4 > 99 ? 99 : l - 4), e->d_name);
			n++;
		}
	}
	if(d) {
		closedir(d);
	}
	qsort(names, (size_t)n, 100, by_name);
	return n;
}

/* the cfg file: "key   value" lines; "artwork type value" */
typedef struct {
	char	rompath[1000], romext[40], artwork_tile[2000], artwork_logo[1000];
} EmuCfg;

static void
expand(const char *in, char *out, size_t n)
{
	const char *home = getenv("HOME");

	if(!strncmp(in, "$HOME", 5) && home) {
		snprintf(out, n, "%s%s", home, in + 5);
	} else {
		profile_expand_path(in, out, (int)n);
	}
}

static void
trim_right(char *s)
{
	size_t n = strlen(s);

	while(n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) {
		s[--n] = 0;
	}
}

static int
read_cfg(const char *dir, const char *emu, EmuCfg *c)
{
	char	path[1300], line[2400];
	FILE	*f;

	memset(c, 0, sizeof(*c));
	snprintf(path, sizeof(path), "%s/emulators/%s.cfg", dir, emu);
	f = fopen(path, "r");
	if(!f) {
		return -1;
	}
	while(fgets(line, sizeof(line), f)) {
		char	*p = line, *val;
		char	key[40], type[40];
		int	n = 0;

		if(line[0] == '#') {
			continue;
		}
		trim_right(line);
		while(*p == ' ' || *p == '\t') {
			p++;
		}
		if(sscanf(p, "%39s%n", key, &n) < 1) {
			continue;
		}
		val = p + n;
		while(*val == ' ' || *val == '\t') {
			val++;
		}
		if(!strcmp(key, "rompath")) {
			expand(val, c->rompath, sizeof(c->rompath));
		} else if(!strcmp(key, "romext")) {
			snprintf(c->romext, sizeof(c->romext), "%s", val);
		} else if(!strcmp(key, "artwork")) {
			int m = 0;

			if(sscanf(val, "%39s%n", type, &m) == 1) {
				char *dirs = val + m, *tok, *save = NULL, one[1100], list[2000];

				while(*dirs == ' ' || *dirs == '\t') {
					dirs++;
				}
				snprintf(list, sizeof(list), "%s", dirs);
				for(tok = strtok_r(list, ";", &save); tok; tok = strtok_r(NULL, ";", &save)) {
					expand(tok, one, sizeof(one));
					if(!one[0]) {
						continue;
					}
					if(!strcmp(type, "wheel") || !strcmp(type, "logo")) {
						snprintf(c->artwork_logo, sizeof(c->artwork_logo), "%s", one);
					} else if(!strcmp(type, "boxart") || !strcmp(type, "flyer") || !strcmp(type, "snap") ||
							!strcmp(type, "marquee")) {
						if(c->artwork_tile[0]) {
							strncat(c->artwork_tile, ";", sizeof(c->artwork_tile) - strlen(c->artwork_tile) - 1);
						}
						strncat(c->artwork_tile, one, sizeof(c->artwork_tile) - strlen(c->artwork_tile) - 1);
					}
				}
			}
		}
	}
	fclose(f);
	return 0;
}

/* ---- reading the game's shell script ---- */

static int
tokenize(const char *s, char toks[][700], int max)
{
	int	n = 0, k = 0, quote = 0;
	char	cur[700];

	for(;; s++) {
		if(*s == 0 || (!quote && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r'))) {
			if(k) {
				cur[k] = 0;
				if(n < max) {
					snprintf(toks[n++], 700, "%s", cur);
				}
				k = 0;
			}
			if(!*s) {
				break;
			}
		} else if(quote && *s == quote) {
			quote = 0;
		} else if(!quote && (*s == '"' || *s == '\'')) {
			quote = *s;
		} else if(k < 699) {
			cur[k++] = *s;
		}
	}
	return n;
}

static void
resolve(const char *rompath, const char *cwd, const char *p, char *out, size_t n)
{
	char	t[1300];
	struct stat sb;

	expand(p, t, sizeof(t));
	if(t[0] == '/') {
		snprintf(out, n, "%s", t);
		return;
	}
	snprintf(out, n, "%s/%s", rompath, t);		/* relative to the game folder first */
	if(stat(out, &sb) != 0 && cwd[0]) {
		char	alt[1400];

		snprintf(alt, sizeof(alt), "%s/%s", cwd, t);
		if(stat(alt, &sb) == 0) {
			snprintf(out, n, "%s", alt);
		}
	}
}

static void
read_script(const char *path, const char *rompath, Profile *p)
{
	FILE	*f = fopen(path, "r");
	char	line[8000], cwd[1100] = "";

	if(!f) {
		return;
	}
	while(fgets(line, sizeof(line), f)) {
		char	(*t)[700];
		int	n, i, start = -1;

		if(line[0] == '#') {
			continue;
		}
		t = malloc(80 * 700);
		if(!t) {
			break;
		}
		n = tokenize(line, t, 80);
		if(n >= 2 && !strcmp(t[0], "cd")) {
			expand(t[1], cwd, sizeof(cwd));
		}
		for(i = 0; i < n; i++) {		/* "... && cd dir && /path/mame64 apple2gs ..." */
			if(!strcmp(t[i], "cd") && i + 1 < n) {
				expand(t[i + 1], cwd, sizeof(cwd));
			}
			if(!strcmp(t[i], "apple2gs") || !strcmp(t[i], "apple2gsr1") || !strcmp(t[i], "apple2gsr3")) {
				start = i + 1;
			}
		}
		for(i = start; start >= 0 && i < n; i++) {
			char	key[16], full[1400];

			if(!strcmp(t[i], "-ramsize") && i + 1 < n) {
				profile_set(p, "ramsize", t[++i]);
			} else if((!strcmp(t[i], "-sl1") || !strcmp(t[i], "-sl2") || !strcmp(t[i], "-sl3") ||
					!strcmp(t[i], "-sl4")) && i + 1 < n) {
				snprintf(key, sizeof(key), "sl%c", t[i][3]);
				profile_set(p, key, t[++i]);
			} else if(!strcmp(t[i], "-sl7") && i + 1 < n) {
				profile_set(p, "sl7", t[++i]);
			} else if((!strncmp(t[i], "-hard", 5) || !strncmp(t[i], "-flop", 5)) && t[i][5] >= '1' &&
					t[i][5] <= '7' && !t[i][6] && i + 1 < n) {
				snprintf(key, sizeof(key), "%.4s%c", t[i] + 1, t[i][5]);
				resolve(rompath, cwd, t[i + 1], full, sizeof(full));
				profile_set(p, key, full);
				i++;
			} else if(!strcmp(t[i], "-mhz") && i + 1 < n) {
				profile_set(p, "mhz", t[++i]);
			} else if(!strcmp(t[i], "-mbspeech") && i + 1 < n) {
				profile_set(p, "mbspeech", t[++i]);
			}
		}
		free(t);
	}
	fclose(f);
}

int
attract_import(const char *dir, const char *emu, Profile *out, int max, char *err, int errsize)
{
	EmuCfg	c;
	char	path[1400], line[2000];
	FILE	*f;
	int	n = 0;

	err[0] = 0;
	if(read_cfg(dir, emu, &c) != 0) {
		snprintf(err, (size_t)errsize, T("Can't read the settings of %s"), emu);
		return -1;
	}
	snprintf(path, sizeof(path), "%s/romlists/%s.txt", dir, emu);
	f = fopen(path, "r");
	if(!f) {
		snprintf(err, (size_t)errsize, T("There is no game list for %s"), emu);
		return -1;
	}
	while(fgets(line, sizeof(line), f) && n < max) {
		char	*name = line, *title, *end, script[1500];
		Profile	*p = &out[n];
		struct stat sb;

		if(line[0] == '#') {
			continue;
		}
		trim_right(line);
		end = strchr(name, ';');
		if(!end) {
			continue;
		}
		*end = 0;
		title = end + 1;
		end = strchr(title, ';');
		if(end) {
			*end = 0;
		}
		profile_init(p);
		profile_set(p, "name", title[0] ? title : name);
		profile_set(p, "description", emu);
		if(c.artwork_tile[0]) {
			profile_set(p, "art_tile", c.artwork_tile);
		}
		if(c.artwork_logo[0]) {
			profile_set(p, "art_logo", c.artwork_logo);
		}
		/* the picture files are named after the rom name, not the title */
		profile_set(p, "art_name", name);
		snprintf(script, sizeof(script), "%s/%s%s", c.rompath, name, c.romext);
		if(stat(script, &sb) == 0) {
			read_script(script, c.rompath, p);
		}
		n++;
	}
	fclose(f);
	return n;
}
