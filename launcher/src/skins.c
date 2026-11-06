#include "skins.h"
#include <string.h>
#include <strings.h>

#define W 0xFFFFFF
#define K 0x000000

static const UiTheme g_skins[] = {
	/* name, bg, fg, dim, line, good, warn, bad, accent, accent_fg, desktop, title, checker */
	{ "Retro", W, K, 0x777777, 0xCCCCCC, 0x00AA00, 0xFF6600, 0xDD0000, K, W, 0x2222FF,
		UI_TITLE_GSOS, 0 },
	{ "Modern", 0x232733, 0xE8EAF0, 0x8C93A6, 0x3B4152, 0x4CC38A, 0xF0A03C, 0xF06464,
		0x4C7DFF, W, 0x14171F, UI_TITLE_FLAT, 0 },
	{ "Magazine", 0xF4ECD8, 0x1B1A17, 0x6F634D, 0xBFB08C, 0x2E7D32, 0xB45F06, 0xB00020,
		0x8B1E1E, 0xF4ECD8, 0xD8CCAE, UI_TITLE_MAGAZINE, 0 },
	{ "MacOS 1", W, K, 0x555555, 0x999999, K, K, K, K, W, K, UI_TITLE_MAC1, 1 },
	{ "MacOS 7", 0xDDDDDD, K, 0x666666, 0xBBBBBB, 0x007700, 0xCC6600, 0xCC0000,
		0x6666CC, W, 0x6F8FB3, UI_TITLE_PLATINUM, 0 },
	{ "Terminal Amber", 0x0A0600, 0xFFB000, 0x8A5F00, 0x5C4000, 0xFFB000, 0xFFD060,
		0xFF6030, 0xFFB000, 0x0A0600, K, UI_TITLE_TERMINAL, 0 },
	{ "Apple II", 0x001400, 0x33FF33, 0x1E9E1E, 0x0E5A0E, 0x33FF33, 0xCCFF33, 0xFF5533,
		0x33FF33, 0x001400, K, UI_TITLE_TERMINAL, 0 },
	{ "NeXT", 0xAAAAAA, K, 0x555555, 0x888888, 0x006600, 0x884400, 0x990000, 0x333333, W,
		0x707070, UI_TITLE_FLAT, 0 },
	{ "BeOS", 0xE8E8E8, K, 0x666666, 0xB0B0B0, 0x007700, 0xCC6600, 0xCC0000, 0xFFCB00, K,
		0x336699, UI_TITLE_FLAT, 0 },
	{ "Retro Neon", 0x120B2E, 0x00F0FF, 0x8A6FD6, 0x3A1F7A, 0x39FF14, 0xFFD300, 0xFF3860,
		0xFF2BD6, W, 0x07031A, UI_TITLE_FLAT, 0 },
	{ "Shelf", 0x2B1D12, 0xF2E4C9, 0xA58B66, 0x5A3F26, 0x7CB342, 0xE0A030, 0xE05050,
		0xC8872B, 0x2B1D12, 0x1A110A, UI_TITLE_FLAT, 0 },
	{ "Cover Flow", 0x101012, 0xF0F0F0, 0x9A9AA0, 0x2A2A30, 0x4CC38A, 0xF0A03C, 0xF06464,
		0x2E5BD8, W, K, UI_TITLE_FLAT, 0 },
};
#define NSKINS ((int)(sizeof(g_skins) / sizeof(g_skins[0])))

int
skin_count(void)
{
	return NSKINS;
}

const UiTheme *
skin_get(int i)
{
	return &g_skins[i >= 0 && i < NSKINS ? i : 0];
}

int
skin_layout(int i)
{
	const char *n = skin_get(i)->name;

	return !strcmp(n, "Shelf") ? SKIN_LAYOUT_SHELF : !strcmp(n, "Cover Flow") ? SKIN_LAYOUT_FLOW :
			SKIN_LAYOUT_LIST;
}

int
skin_desktop(int i)
{
	return !strcmp(skin_get(i)->name, "Retro Neon") ? SKIN_DESK_NEON : SKIN_DESK_PLAIN;
}

int
skin_find(const char *name)
{
	for(int i = 0; i < NSKINS; i++) {
		if(!strcasecmp(g_skins[i].name, name)) {
			return i;
		}
	}
	return -1;
}

void
skin_apply(int i)
{
	g_ui_theme = *skin_get(i);
}
