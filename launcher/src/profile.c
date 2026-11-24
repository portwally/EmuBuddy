#include "lang.h"
#include "profile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <strings.h>

void
profile_init(Profile *p)
{
	memset(p, 0, sizeof(*p));
}

static void
trim(char *s)
{
	size_t	n = strlen(s), a = 0;

	while(n > 0 && isspace((unsigned char)s[n - 1])) {
		s[--n] = 0;
	}
	while(s[a] && isspace((unsigned char)s[a])) {
		a++;
	}
	if(a) {
		memmove(s, s + a, strlen(s + a) + 1);
	}
}

const char *
profile_get(const Profile *p, const char *key)
{
	int	i;

	for(i = 0; i < p->n; i++) {
		if(!strcasecmp(p->kv[i].key, key)) {
			return p->kv[i].value;
		}
	}
	return "";
}

void
profile_set(Profile *p, const char *key, const char *value)
{
	int	i;

	for(i = 0; i < p->n; i++) {
		if(!strcasecmp(p->kv[i].key, key)) {
			snprintf(p->kv[i].value, sizeof(p->kv[i].value), "%s", value);
			return;
		}
	}
	if(p->n < PROFILE_MAX_KEYS) {
		snprintf(p->kv[p->n].key, sizeof(p->kv[0].key), "%s", key);
		snprintf(p->kv[p->n].value, sizeof(p->kv[0].value), "%s", value);
		p->n++;
	}
}

int
profile_load(Profile *p, const char *path)
{
	FILE	*f = fopen(path, "r");
	char	line[900], pending[200] = "";

	profile_init(p);
	if(!f) {
		return -1;
	}
	snprintf(p->path, sizeof(p->path), "%s", path);
	while(fgets(line, sizeof(line), f)) {
		char	*eq, *hash;

		if(line[0] == '#') {			/* a whole-line comment */
			line[strcspn(line, "\r\n")] = 0;
			snprintf(pending, sizeof(pending), "%s", line);
			continue;
		}
		eq = strchr(line, '=');
		if(!eq) {
			continue;
		}
		*eq = 0;
		trim(line);
		eq++;
		hash = strstr(eq, " #");		/* comment after the value */
		if(hash) {
			*hash = 0;
		}
		trim(eq);
		if(line[0]) {
			if(p->n < PROFILE_MAX_KEYS) {
				snprintf(p->comments[p->n], sizeof(p->comments[0]),
								"%s", pending);
			}
			profile_set(p, line, eq);
			pending[0] = 0;
		}
	}
	fclose(f);
	return 0;
}

int
profile_save(const Profile *p, const char *path)
{
	FILE	*f = fopen(path, "w");
	int	i;

	if(!f) {
		return -1;
	}
	for(i = 0; i < p->n; i++) {
		if(p->comments[i][0]) {
			fprintf(f, "%s\n", p->comments[i]);
		}
		fprintf(f, "%s = %s\n", p->kv[i].key, p->kv[i].value);
	}
	return fclose(f) == 0 ? 0 : -1;
}

const char *
profile_name(const Profile *p)
{
	static char	name[200];
	const char	*n = profile_get(p, "name"), *b, *dot;

	if(n[0]) {
		return n;
	}
	b = strrchr(p->path, '/');
	b = b ? b + 1 : p->path;
	snprintf(name, sizeof(name), "%s", b);
	dot = strrchr(name, '.');
	if(dot) {
		name[dot - name] = 0;
	}
	return name;
}

void
profile_expand_path(const char *in, char *out, int outsize)
{
	const char	*home = getenv("HOME");

	if(in[0] == '~' && (in[1] == '/' || !in[1]) && home) {
		snprintf(out, (size_t)outsize, "%s%s", home, in + 1);
	} else {
		snprintf(out, (size_t)outsize, "%s", in);
	}
}

/* ---- command line ---- */

typedef struct {
	char	*buf;
	int	size, used;
	char	**argv;
	int	max, argc;
	int	overflow;
} Out;

static void
add(Out *o, const char *s)
{
	size_t	n = strlen(s) + 1;

	if(o->argc >= o->max - 1 || o->used + (int)n > o->size) {
		o->overflow = 1;
		return;
	}
	memcpy(o->buf + o->used, s, n);
	o->argv[o->argc++] = o->buf + o->used;
	o->used += (int)n;
	o->argv[o->argc] = NULL;
}

static void
add_path(Out *o, const char *opt, const char *path)
{
	char	full[1200];

	profile_expand_path(path, full, sizeof(full));
	add(o, opt);
	add(o, full);
}

static int
is_off(const char *v)
{
	return !v[0] || !strcasecmp(v, "0") || !strcasecmp(v, "off") ||
					!strcasecmp(v, "no") || !strcasecmp(v, "false");
}

/* Split "a b 'c d'" into arguments. */
static void
add_words(Out *o, const char *s)
{
	char	word[600];
	int	n = 0, quote = 0;

	for(;; s++) {
		if(*s == 0 || (!quote && isspace((unsigned char)*s))) {
			if(n) {
				word[n] = 0;
				add(o, word);
				n = 0;
			}
			if(!*s) {
				break;
			}
		} else if(quote && *s == quote) {
			quote = 0;
		} else if(!quote && (*s == '"' || *s == '\'')) {
			quote = *s;
		} else if(n < (int)sizeof(word) - 1) {
			word[n++] = *s;
		}
	}
}

int
profile_build_args(const Profile *p, const char *exe, const char *cfgdir,
		char *buf, int bufsize, char **argv, int maxargs, char *err,
		int errsize)
{
	Out		o = { buf, bufsize, 0, argv, maxargs, 0, 0 };
	char		key[16], cfg[1200];
	const char	*v;
	int		i;

	err[0] = 0;
	add(&o, exe);
	add(&o, "apple2gs");
	if((v = profile_get(p, "rom"))[0]) {
		add_path(&o, "-rom", v);
	} else if((v = profile_get(p, "rompath"))[0]) {
		add_path(&o, "-rompath", v);
	}
	if((v = profile_get(p, "ramsize"))[0]) {
		add(&o, "-ramsize");
		add(&o, v);
	}
	if((v = profile_get(p, "mhz"))[0]) {
		add(&o, "-mhz");
		add(&o, v);
	}
	for(i = 1; i <= 7; i++) {
		snprintf(key, sizeof(key), "sl%d", i);
		if((v = profile_get(p, key))[0]) {
			if(i == 5 || i == 6) {
				snprintf(err, (size_t)errsize, T("slot %d holds the built-in drives"), i);
				return -1;
			}
			add(&o, i == 1 ? "-sl1" : i == 2 ? "-sl2" : i == 3 ? "-sl3" :
				i == 4 ? "-sl4" : "-sl7");
			add(&o, v);
		}
	}
	for(i = 1; i <= 4; i++) {
		snprintf(key, sizeof(key), "flop%d", i);
		if((v = profile_get(p, key))[0]) {
			snprintf(cfg, sizeof(cfg), "-flop%d", i);
			add_path(&o, cfg, v);
		}
	}
	for(i = 1; i <= 7; i++) {
		snprintf(key, sizeof(key), "hard%d", i);
		if((v = profile_get(p, key))[0]) {
			snprintf(cfg, sizeof(cfg), "-hard%d", i);
			add_path(&o, cfg, v);
		}
	}
	if((v = profile_get(p, "cdrom"))[0]) {
		add_path(&o, "-cdrom", v);
	}
	if((v = profile_get(p, "hsscsirom"))[0]) {
		add_path(&o, "-hsscsirom", v);
	}
	if((v = profile_get(p, "share"))[0]) {
		add_path(&o, "-share", v);
	}
	if((v = profile_get(p, "bridge"))[0]) {
		add_path(&o, "-bridge", v);
	}
	if((v = profile_get(p, "scale"))[0]) {
		add(&o, "-scale");
		add(&o, v);
	} else if((v = profile_get(p, "resolution"))[0]) {
		add(&o, "-resolution");
		add(&o, v);
	}
	if(!is_off(profile_get(p, "fullscreen"))) {
		add(&o, "-nowindow");
	}
	if(!is_off(profile_get(p, "crt"))) {
		add(&o, "-crt");
	}
	if((v = profile_get(p, "statusbar"))[0] && is_off(v)) {
		add(&o, "-nostatus");
	}
	if((v = profile_get(p, "scanlines"))[0]) {
		add(&o, "-scanlines");
		add(&o, v);
	}
	if((v = profile_get(p, "skin"))[0]) {
		add(&o, "-skin");
		add(&o, v);
	}
	add(&o, "-profile");
	add(&o, profile_name(p));
	if((v = profile_get(p, "title"))[0]) {
		add(&o, "-title");
		add(&o, v);
	}
	if(cfgdir && cfgdir[0]) {
		snprintf(cfg, sizeof(cfg), "%s/emubuddy.config", cfgdir);
		add(&o, "-cfg");
		add(&o, cfg);
	}
	if(cfgdir && cfgdir[0] && !((v = profile_get(p, "autosnap"))[0] && is_off(v))) {
		/* EmuBuddy leaves a picture of the last screen there; the Launcher shows it for a game without a cover */
		snprintf(cfg, sizeof(cfg), "%s/snap.png", cfgdir);
		add(&o, "-quitshot");
		add(&o, cfg);
	}
	if((v = profile_get(p, "extra"))[0]) {
		add_words(&o, v);
	}
	if(o.overflow) {
		snprintf(err, (size_t)errsize, "%s", T("the profile is too large"));
		return -1;
	}
	return o.argc;
}

void
profile_cmdline(char **argv, int argc, char *out, int outsize)
{
	int	i, n = 0;

	out[0] = 0;
	for(i = 0; i < argc && n < outsize - 4; i++) {
		int	quote = strpbrk(argv[i], " \t\"'$&;()") != NULL;

		n += snprintf(out + n, (size_t)(outsize - n), "%s%s%s%s", i ? " " : "",
					quote ? "\"" : "", argv[i], quote ? "\"" : "");
	}
}

const char *
eb_strcasestr(const char *hay, const char *needle)
{
	size_t	n = strlen(needle);

	if(!n) {
		return hay;
	}
	for(; *hay; hay++) {
		if(!strncasecmp(hay, needle, n)) {
			return hay;
		}
	}
	return NULL;
}
