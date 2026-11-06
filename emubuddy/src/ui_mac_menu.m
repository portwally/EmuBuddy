/* EmuBuddy (macOS): the "System" skin's menu bar. The same menu tables the
 * drawn menu bar uses (ui.h) become the real macOS menu bar. Choosing an item
 * posts its command; the emulator runs it on the next frame.
 *
 * Shortcuts are shown for information only: the keys themselves are handled
 * by the emulator window, so the IIgs keeps Open-Apple (Command) combinations.
 * The one exception is Option-Command-Q (Quit). */
#import <Cocoa/Cocoa.h>
#include "ui.h"
#include <string.h>

const UiApp	*ui_get_app(void);
void		ui_post_command(int id);

@interface EBMenu : NSMenu
@property (nonatomic) int uiIndex;		/* index in the UiApp menu table, -1 = main */
@end

@interface EBTarget : NSObject <NSMenuDelegate>
- (void)action:(NSMenuItem *)sender;
@end

static EBTarget	*g_target;
static NSMenu	*g_main;			/* our menu bar */
static NSMenu	*g_saved;			/* SDL's own menu bar, restored when leaving */
static int	g_about_id, g_quit_id;

@implementation EBMenu
- (BOOL)performKeyEquivalent:(NSEvent *)event
{
	/* only Option-Command-Q is real; every other key goes to the emulator */
	if(([event modifierFlags] & NSEventModifierFlagCommand) &&
			([event modifierFlags] & NSEventModifierFlagOption)) {
		return [super performKeyEquivalent:event];
	}
	return NO;
}
@end

@implementation EBTarget
- (void)action:(NSMenuItem *)sender
{
	ui_post_command((int)[sender tag]);
}

- (void)menuNeedsUpdate:(NSMenu *)menu
{
	EBMenu	*m = (EBMenu *)menu;

	if([m uiIndex] >= 0) {
		extern void ebmenu_fill(NSMenu *m, int idx);
		ebmenu_fill(m, [m uiIndex]);
	}
}
@end

/* "Shift-F10" / "Ctrl-F11" / "F2" -> key equivalent (display only) */
static void
set_shortcut(NSMenuItem *mi, const char *sc)
{
	NSEventModifierFlags	mods = 0;
	const char		*p = sc, *dash;
	int			n;

	while((dash = strchr(p, '-')) != NULL) {
		if(!strncmp(p, "Ctrl", 4)) mods |= NSEventModifierFlagControl;
		else if(!strncmp(p, "Shift", 5)) mods |= NSEventModifierFlagShift;
		else if(!strncmp(p, "Alt", 3)) mods |= NSEventModifierFlagOption;
		p = dash + 1;
	}
	if(p[0] == 'F' && (n = atoi(p + 1)) >= 1 && n <= 12) {
		unichar	c = (unichar)(NSF1FunctionKey + n - 1);

		[mi setKeyEquivalent:[NSString stringWithCharacters:&c length:1]];
		[mi setKeyEquivalentModifierMask:mods];
	}
}

void
ebmenu_fill(NSMenu *m, int idx)
{
	const UiApp	*app = ui_get_app();
	const UiMenu	*um = &app->menus[idx];
	int		i;

	[m removeAllItems];
	for(i = 0; i < um->n_items; i++) {
		const UiMenuItem *it = &um->items[i];
		NSMenuItem	*mi;
		int		st;

		if(it->flags & UI_MI_SEP) {
			NSMenuItem *sep = [NSMenuItem separatorItem];

			/* no separator first, last or doubled */
			if([m numberOfItems] > 0 && ![[m itemAtIndex:[m numberOfItems] - 1] isSeparatorItem]) {
				[m addItem:sep];
			}
			continue;
		}
		if(!strncmp(it->label, "Quit", 4) || !strncmp(it->label, "About", 5)) {
			continue;		/* these are in the application menu */
		}
		mi = [[NSMenuItem alloc] initWithTitle:[NSString stringWithUTF8String:it->label]
				action:@selector(action:) keyEquivalent:@""];
		[mi setTarget:g_target];
		[mi setTag:it->id];
		st = it->flags | (app->item_state ? app->item_state(it->id) : 0);
		[mi setEnabled:!(st & UI_MI_DISABLED)];
		[mi setState:(st & UI_MI_CHECKED) ? NSControlStateValueOn : NSControlStateValueOff];
		if(it->shortcut) {
			set_shortcut(mi, it->shortcut);
		}
		[m addItem:mi];
		[mi release];
	}
	if([m numberOfItems] > 0 && [[m itemAtIndex:[m numberOfItems] - 1] isSeparatorItem]) {
		[m removeItemAtIndex:[m numberOfItems] - 1];
	}
}

static void
find_ids(const UiApp *app)
{
	int	i, k;

	g_about_id = g_quit_id = 0;
	for(i = 0; i < app->n_menus; i++) {
		for(k = 0; k < app->menus[i].n_items; k++) {
			const UiMenuItem *it = &app->menus[i].items[k];

			if(!(it->flags & UI_MI_SEP)) {
				if(!strncmp(it->label, "About", 5)) g_about_id = it->id;
				if(!strncmp(it->label, "Quit", 4)) g_quit_id = it->id;
			}
		}
	}
}

static NSMenu *
build_menu_bar(void)
{
	const UiApp	*app = ui_get_app();
	EBMenu		*bar = [[EBMenu alloc] initWithTitle:@""];
	EBMenu		*appm = [[EBMenu alloc] initWithTitle:@"EmuBuddy"];
	NSMenuItem	*item, *mi;
	int		i;

	[bar setAutoenablesItems:NO];
	[bar setUiIndex:-1];
	[appm setAutoenablesItems:NO];
	[appm setUiIndex:-1];
	find_ids(app);

	/* application menu: About, Quit (Option-Command-Q) */
	mi = [[NSMenuItem alloc] initWithTitle:@"About EmuBuddy"
			action:@selector(action:) keyEquivalent:@""];
	[mi setTarget:g_target];
	[mi setTag:g_about_id];
	[appm addItem:mi];
	[mi release];
	[appm addItem:[NSMenuItem separatorItem]];
	mi = [[NSMenuItem alloc] initWithTitle:@"Quit EmuBuddy"
			action:@selector(action:) keyEquivalent:@"q"];
	[mi setKeyEquivalentModifierMask:NSEventModifierFlagOption | NSEventModifierFlagCommand];
	[mi setTarget:g_target];
	[mi setTag:g_quit_id];
	[appm addItem:mi];
	[mi release];
	item = [[NSMenuItem alloc] initWithTitle:@"EmuBuddy" action:nil keyEquivalent:@""];
	[item setSubmenu:appm];
	[bar addItem:item];
	[item release];
	[appm release];

	for(i = 0; i < app->n_menus; i++) {
		EBMenu *sub = [[EBMenu alloc] initWithTitle:
				[NSString stringWithUTF8String:app->menus[i].title]];

		[sub setAutoenablesItems:NO];
		[sub setUiIndex:i];
		[sub setDelegate:g_target];
		ebmenu_fill(sub, i);
		item = [[NSMenuItem alloc] initWithTitle:[sub title] action:nil
							keyEquivalent:@""];
		[item setSubmenu:sub];
		[bar addItem:item];
		[item release];
		[sub release];
	}
	return bar;
}

/* Show the system menu bar (on = 1) or give back SDL's (on = 0). */
void
mac_native_menu_enable(int on)
{
	@autoreleasepool {
		if(!g_target) {
			g_target = [[EBTarget alloc] init];
		}
		if(on) {
			if(!g_saved) {
				g_saved = [[NSApp mainMenu] retain];
			}
			if(g_main) {
				[g_main release];
			}
			g_main = build_menu_bar();
			[NSApp setMainMenu:g_main];
		} else if(g_saved) {
			[NSApp setMainMenu:g_saved];
		}
	}
}
