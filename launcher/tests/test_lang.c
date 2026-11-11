/* Tests for the German texts (launcher/src/lang.c): every pair has the same
 * printf directives in the same order (a wrong one would crash the program),
 * no text is there twice, and the switch works. */
#include "lang.h"
#include <stdio.h>
#include <string.h>

static int	g_fail;

/* the directives of a format string, one after the other: "%s%ld" -> "s;ld;" */
static void
directives(const char *s, char *out, size_t n)
{
	size_t	k = 0;

	for(; *s; s++) {
		if(s[0] != '%') {
			continue;
		}
		s++;
		if(*s == '%') {
			continue;
		}
		while(*s && strchr("-+ #0123456789.", *s)) {
			s++;
		}
		while(*s && strchr("lhz", *s) && k + 2 < n) {
			out[k++] = *s++;
		}
		if(*s && k + 2 < n) {
			out[k++] = *s;
			out[k++] = ';';
		}
		if(!*s) {
			break;
		}
	}
	out[k] = 0;
}

int
main(void)
{
	const char	*en, *de, *en2, *de2;
	char		a[100], b[100];
	int		i, j;

	for(i = 0; i < lang_count(); i++) {
		lang_entry(i, &en, &de);
		directives(en, a, sizeof(a));
		directives(de, b, sizeof(b));
		if(strcmp(a, b) != 0) {
			printf("FAIL: \"%s\" has %s but its German text has %s\n", en, a[0] ? a : "none", b[0] ? b : "none");
			g_fail++;
		}
		if(!de[0]) {
			printf("FAIL: \"%s\" has an empty German text\n", en);
			g_fail++;
		}
		for(j = 0; j < i; j++) {
			lang_entry(j, &en2, &de2);
			if(!strcmp(en, en2)) {
				printf("FAIL: \"%s\" is in the table twice\n", en);
				g_fail++;
			}
		}
	}
	if(strcmp(lang_tr("Cancel"), "Cancel") != 0) {
		printf("FAIL: English is not the default\n");
		g_fail++;
	}
	lang_set_german(1);
	if(strcmp(lang_tr("Cancel"), "Abbrechen") != 0 || strcmp(lang_tr("not in the table"), "not in the table") != 0) {
		printf("FAIL: German on: Cancel -> %s\n", lang_tr("Cancel"));
		g_fail++;
	}
	lang_set_plain(1);
	if(strcmp(lang_tr("Delete"), "Loeschen") != 0 || strcmp(lang_tr("Close"), "Schliessen") != 0) {
		printf("FAIL: plain letters: %s, %s\n", lang_tr("Delete"), lang_tr("Close"));
		g_fail++;
	}
	lang_set_plain(0);
	lang_set_german(0);
	if(strcmp(lang_tr("Delete"), "Delete") != 0) {
		printf("FAIL: German off again\n");
		g_fail++;
	}
	printf(g_fail ? "%d test(s) failed\n" : "lang: all tests passed (%d texts)\n", g_fail ? g_fail : lang_count());
	return g_fail ? 1 : 0;
}
