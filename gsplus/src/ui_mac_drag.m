/* EmuBuddy (macOS): drag a file out of the SDL window to the Finder. SDL
 * only receives drops, so this starts a native AppKit drag session with a
 * file URL; the Finder copies the file where it is dropped. */
#import <Cocoa/Cocoa.h>
#include <SDL3/SDL.h>

@interface EBDragSource : NSObject <NSDraggingSource>
@end

@implementation EBDragSource
- (NSDragOperation)draggingSession:(NSDraggingSession *)session
	sourceOperationMaskForDraggingContext:(NSDraggingContext)context
{
	(void)session;
	return context == NSDraggingContextOutsideApplication ?
				NSDragOperationCopy : NSDragOperationNone;
}
@end

int
mac_drag_file_out(SDL_Window *win, const char *path)
{
	static EBDragSource	*source;
	NSWindow	*nw;
	NSView		*view;
	NSEvent		*ev;
	NSURL		*url;
	NSDraggingItem	*item;
	NSImage		*icon;
	NSPoint		p;

	nw = (__bridge NSWindow *)SDL_GetPointerProperty(
			SDL_GetWindowProperties(win),
			SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
	if(!nw || !path) {
		return -1;
	}
	view = [nw contentView];
	ev = [NSApp currentEvent];
	if(!ev || ([ev type] != NSEventTypeLeftMouseDragged &&
				[ev type] != NSEventTypeLeftMouseDown) ||
				[ev window] != nw) {
		/* make a drag event at the current pointer position */
		ev = [NSEvent mouseEventWithType:NSEventTypeLeftMouseDragged
			location:[nw mouseLocationOutsideOfEventStream]
			modifierFlags:0 timestamp:[[NSProcessInfo processInfo]
						systemUptime]
			windowNumber:[nw windowNumber] context:nil
			eventNumber:0 clickCount:1 pressure:1.0];
	}
	if(!source) {
		source = [[EBDragSource alloc] init];
	}
	url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path]];
	item = [[[NSDraggingItem alloc] initWithPasteboardWriter:url]
							autorelease];
	icon = [[NSWorkspace sharedWorkspace] iconForFile:[url path]];
	[icon setSize:NSMakeSize(32, 32)];
	p = [view convertPoint:[ev locationInWindow] fromView:nil];
	[item setDraggingFrame:NSMakeRect(p.x - 16, p.y - 16, 32, 32)
							contents:icon];
	@try {
		[view beginDraggingSessionWithItems:@[item] event:ev
							source:source];
	} @catch (NSException *e) {
		(void)e;
		return -1;
	}
	return 0;
}
