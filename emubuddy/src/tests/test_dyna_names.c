/* Shared folder names test: a text file is NAME.txt on the host (not
 * NAME,ttxt), and the ".txt" is not part of the IIgs name, so names go
 * both ways unchanged. Exit code 0 when everything passes.
 *   cmake --build build --target test_dyna_names && build/test_dyna_names */
#include "../dyna_type.c"		/* brings defc.h along */

/* What dyna_type.c needs from config.c */
int cfgcasecmp(const char *a, const char *b) { return strcasecmp(a, b); }
int cfg_strlcat(char *d, const char *s, int n) {
	int l = (int)strlen(d);
	snprintf(d + l, n > l ? (size_t)(n - l) : 0, "%s", s);
	return l + (int)strlen(s);
}
const char *cfg_str_basename(const char *s) {
	const char *p = strrchr(s, '/');
	return p ? p + 1 : s;
}
void halt_printf(const char *fmt, ...) { (void)fmt; printf("  HALT\n"); exit(1); }

static int g_fail;
#define CHECK(c, msg) do { if(c) printf("  ok   %s\n", msg); \
	else { printf("  FAIL %s\n", msg); g_fail++; } } while(0)

/* The host name the IIgs file NAME gets */
static const char *
host_name(const char *name, int type, int aux)
{
	static char	buf[256];
	Dynapro_file	f;

	memset(&f, 0, sizeof(f));
	f.prodos_name[0] = 0x10 | (byte)strlen(name);
	f.file_type = type;
	f.aux_type = aux;
	snprintf(buf, sizeof(buf), "/x/%s", name);
	dynatype_fix_unix_name(&f, buf, sizeof(buf));
	return buf + 3;
}

/* The IIgs name part of a host name (before any type suffix), and its type */
static const char *
gs_name(const char *host, int *type, int *aux)
{
	static char	buf[256];
	Dynapro_file	f;
	int		end;

	memset(&f, 0, sizeof(f));
	dynatype_detect_file_type(&f, host, 0x10);
	*type = f.file_type;
	*aux = f.aux_type;
	end = dynatype_name_end(host, 0, (int)strlen(host), f.file_type);
	snprintf(buf, sizeof(buf), "%.*s", end, host);
	if(strchr(buf, ',')) {
		*strchr(buf, ',') = 0;
	}
	return buf;
}

static void
both_ways(const char *name, int type, int aux, const char *want_host)
{
	char		msg[200];
	const char	*h;
	int		t, a;

	h = host_name(name, type, aux);
	snprintf(msg, sizeof(msg), "%s ($%02X/$%04X) is \"%s\" on the host "
						"(got \"%s\")", name, type, aux, want_host, h);
	CHECK(!strcmp(h, want_host), msg);
	snprintf(msg, sizeof(msg), "\"%s\" is %s again", want_host, name);
	CHECK(!strcmp(gs_name(want_host, &t, &a), name) && t == type && a == aux,
									msg);
}

int
main(void)
{
	int	t, a;

	printf("Text files from the IIgs\n");
	both_ways("Install.List", 0x04, 0, "Install.List.txt");
	both_ways("ReadMe.Release", 0x04, 0, "ReadMe.Release.txt");
	both_ways("Notes.txt", 0x04, 0, "Notes.txt.txt");
	both_ways("Data", 0x04, 0x0080, "Data.txt,a$0080");
	printf("Other files keep their type suffix\n");
	both_ways("Finder.Data", 0xc9, 0, "Finder.Data,tfnd");
	both_ways("Prog", 0x06, 0x2000, "Prog,tbin");
	both_ways("Basic", 0xfc, 0x0801, "Basic,tbas");
	printf("Files from the Mac\n");
	CHECK(!strcmp(gs_name("Read Me.txt", &t, &a), "Read Me") && t == 4,
					"\"Read Me.txt\" is a text file without .txt");
	CHECK(!strcmp(gs_name("Old,ttxt", &t, &a), "Old") && t == 4,
					"an old \"Old,ttxt\" is still a text file");
	CHECK(!strcmp(gs_name(".txt", &t, &a), ".txt"),
					"a name that is only \".txt\" keeps it");
	CHECK(!strcmp(gs_name("Photo.jpg", &t, &a), "Photo.jpg") && t != 4,
					"other Mac files are unchanged");
	printf(g_fail ? "%d FAILED\n" : "all name tests passed\n", g_fail);
	return g_fail ? 1 : 0;
}
