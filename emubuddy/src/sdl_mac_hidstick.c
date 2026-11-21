/**********************************************************************/
/*                    GSplus - Apple //gs Emulator                    */
/*                    Based on KEGS by Kent Dickey                    */
/*                    Copyright 2002-2023 Kent Dickey                 */
/*                    Copyright 2025-2026 GSplus Contributors         */
/*                                                                    */
/*      This code is covered by the GNU GPL v3                        */
/*      See the file COPYING.txt or https://www.gnu.org/licenses/     */
/**********************************************************************/

/* macOS: a plain USB joystick, read straight from IOKit.
 *
 * sdl_driver.c takes game controllers from SDL's Gamepad API. A plain HID
 * joystick (two axes and a few buttons, no gamepad layout: an arcade stick,
 * a retro stick such as THECXSTICK) never arrives there: macOS's
 * GameController framework says it supports the device, so SDL leaves it
 * to that framework, which then never reports it - and even if SDL saw it,
 * it has no gamepad mapping for it. So when SDL has no gamepad, the first
 * HID joystick / gamepad / multi-axis controller is read here instead:
 * X and Y, a hat switch if there is one, and the buttons (odd-numbered:
 * IIgs button 0, even-numbered: button 1, as the native Mac backend in
 * joystick_driver.c does). Plugging in and pulling out are followed. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDManager.h>
#include <IOKit/hid/IOHIDKeys.h>
#include <IOKit/hid/IOHIDUsageTables.h>

#define HIDSTICK_MAX_BTN	32

static IOHIDManagerRef g_hidstick_mgr = NULL;
static IOHIDDeviceRef g_hidstick_dev = NULL;	/* the stick in use, or NULL */
static CFArrayRef g_hidstick_els = NULL;	/* keeps its elements alive */
static IOHIDElementRef g_hidstick_x = NULL;
static IOHIDElementRef g_hidstick_y = NULL;
static IOHIDElementRef g_hidstick_hat = NULL;
static IOHIDElementRef g_hidstick_btn[HIDSTICK_MAX_BTN];	/* by usage - 1 */

/* What the stick itself has reported since it was opened (the input value
 * callback): these count before a value asked for (IOHIDDeviceGetValue),
 * which on some devices stays at what it was when the device was opened. */
static long g_hidstick_rep_x, g_hidstick_rep_y, g_hidstick_rep_hat;
static int g_hidstick_has_x, g_hidstick_has_y, g_hidstick_has_hat;
static unsigned int g_hidstick_rep_btn, g_hidstick_has_btn;	/* bit: usage - 1 */

/* EMUBUDDY_JOYDEBUG=1 in the environment: what the stick reports, and what
 * is passed on, is printed */
static int g_hidstick_debug = 0;

static char g_hidstick_name[128];	/* the stick's product name */
static int g_hidstick_dpad;		/* hat at the last read: 1 up 2 right 4 down 8 left */

static void
hidstick_drop(void)
{
	if(g_hidstick_els) {
		CFRelease(g_hidstick_els);
	}
	if(g_hidstick_dev) {
		CFRelease(g_hidstick_dev);
	}
	g_hidstick_els = NULL;
	g_hidstick_dev = NULL;
	g_hidstick_x = NULL;
	g_hidstick_y = NULL;
	g_hidstick_hat = NULL;
	memset(g_hidstick_btn, 0, sizeof(g_hidstick_btn));
	g_hidstick_has_x = 0;
	g_hidstick_has_y = 0;
	g_hidstick_has_hat = 0;
	g_hidstick_has_btn = 0;
	g_hidstick_rep_btn = 0;
}

/* Take this device if there is none yet and it has an X and a Y axis */
static void
hidstick_adopt(IOHIDDeviceRef dev)
{
	IOHIDElementRef el, x, y, hat, btn[HIDSTICK_MAX_BTN];
	CFArrayRef els;
	CFTypeRef name_ref;
	CFIndex	num, i;
	char	name[128];
	int	type, page, usage;

	if(g_hidstick_dev || !dev) {
		return;
	}
	els = IOHIDDeviceCopyMatchingElements(dev, NULL, kIOHIDOptionsTypeNone);
	if(!els) {
		return;
	}
	x = NULL;
	y = NULL;
	hat = NULL;
	memset(btn, 0, sizeof(btn));
	num = CFArrayGetCount(els);
	for(i = 0; i < num; i++) {
		el = (IOHIDElementRef)CFArrayGetValueAtIndex(els, i);
		type = IOHIDElementGetType(el);
		if((type != kIOHIDElementTypeInput_Misc) &&
				(type != kIOHIDElementTypeInput_Button) &&
				(type != kIOHIDElementTypeInput_Axis)) {
			continue;
		}
		page = IOHIDElementGetUsagePage(el);
		usage = IOHIDElementGetUsage(el);
		if(page == kHIDPage_GenericDesktop) {
			if((usage == kHIDUsage_GD_X) && !x) {
				x = el;
			} else if((usage == kHIDUsage_GD_Y) && !y) {
				y = el;
			} else if((usage == kHIDUsage_GD_Hatswitch) && !hat) {
				hat = el;
			}
		} else if((page == kHIDPage_Button) && (usage >= 1) &&
					(usage <= HIDSTICK_MAX_BTN)) {
			if(!btn[usage - 1]) {
				btn[usage - 1] = el;
			}
		}
	}
	if(!x || !y) {
		CFRelease(els);
		return;
	}
	CFRetain(dev);
	g_hidstick_dev = dev;
	g_hidstick_els = els;
	g_hidstick_x = x;
	g_hidstick_y = y;
	g_hidstick_hat = hat;
	memcpy(g_hidstick_btn, btn, sizeof(btn));

	strcpy(name, "(no name)");
	name_ref = IOHIDDeviceGetProperty(dev, CFSTR(kIOHIDProductKey));
	if(name_ref && (CFGetTypeID(name_ref) == CFStringGetTypeID())) {
		CFStringGetCString((CFStringRef)name_ref, name, sizeof(name),
						kCFStringEncodingUTF8);
	}
	snprintf(g_hidstick_name, sizeof(g_hidstick_name), "%s", name);
	printf("USB joystick opened: %s\n", name);
}

/* Take the first usable one of the devices that are there now */
static void
hidstick_adopt_any(void)
{
	IOHIDDeviceRef devs[64];
	CFSetRef set;
	CFIndex	num, i;

	if(g_hidstick_dev || !g_hidstick_mgr) {
		return;
	}
	set = IOHIDManagerCopyDevices(g_hidstick_mgr);
	if(!set) {
		return;
	}
	num = CFSetGetCount(set);
	if(num <= 64) {
		CFSetGetValues(set, (const void **)devs);
		for(i = 0; i < num; i++) {
			hidstick_adopt(devs[i]);
		}
	}
	CFRelease(set);
}

/* The stick reports a value (from the main run loop) */
static void
hidstick_value(void *ctx, IOReturn result, void *sender, IOHIDValueRef value)
{
	IOHIDElementRef el;
	long	val;
	int	page, usage;

	(void)ctx;
	(void)result;
	if(!g_hidstick_dev || !value) {
		return;
	}
	el = IOHIDValueGetElement(value);
	if(!el || (IOHIDElementGetDevice(el) != g_hidstick_dev)) {
		return;
	}
	(void)sender;
	page = IOHIDElementGetUsagePage(el);
	usage = IOHIDElementGetUsage(el);
	val = (long)IOHIDValueGetIntegerValue(value);
	if(g_hidstick_debug) {
		printf("USB joystick reports: page %d usage %d = %ld\n", page,
								usage, val);
		fflush(stdout);
	}
	if(page == kHIDPage_GenericDesktop) {
		if(usage == kHIDUsage_GD_X) {
			g_hidstick_rep_x = val;
			g_hidstick_has_x = 1;
		} else if(usage == kHIDUsage_GD_Y) {
			g_hidstick_rep_y = val;
			g_hidstick_has_y = 1;
		} else if(usage == kHIDUsage_GD_Hatswitch) {
			g_hidstick_rep_hat = val;
			g_hidstick_has_hat = 1;
		}
	} else if((page == kHIDPage_Button) && (usage >= 1) &&
					(usage <= HIDSTICK_MAX_BTN)) {
		g_hidstick_has_btn |= 1U << (usage - 1);
		if(val != 0) {
			g_hidstick_rep_btn |= 1U << (usage - 1);
		} else {
			g_hidstick_rep_btn &= ~(1U << (usage - 1));
		}
	}
}

static void
hidstick_added(void *ctx, IOReturn result, void *sender, IOHIDDeviceRef dev)
{
	(void)ctx;
	(void)result;
	(void)sender;
	hidstick_adopt(dev);
}

static void
hidstick_removed(void *ctx, IOReturn result, void *sender, IOHIDDeviceRef dev)
{
	(void)ctx;
	(void)result;
	(void)sender;
	if(dev == g_hidstick_dev) {
		printf("USB joystick removed\n");
		hidstick_drop();
		hidstick_adopt_any();
	}
}

static CFDictionaryRef
hidstick_match(int usage)
{
	CFMutableDictionaryRef dict;
	CFNumberRef num;
	int	page;

	dict = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
					&kCFTypeDictionaryKeyCallBacks,
					&kCFTypeDictionaryValueCallBacks);
	page = kHIDPage_GenericDesktop;
	num = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &page);
	CFDictionarySetValue(dict, CFSTR(kIOHIDDeviceUsagePageKey), num);
	CFRelease(num);
	num = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &usage);
	CFDictionarySetValue(dict, CFSTR(kIOHIDDeviceUsageKey), num);
	CFRelease(num);
	return dict;
}

/* Called once at startup, on the main thread. The added/removed callbacks
 * run from the main run loop, which SDL's event pump turns every frame. */
void
mac_hidstick_init(void)
{
	CFDictionaryRef dicts[3];
	CFArrayRef matches;
	int	i;

	if(g_hidstick_mgr) {
		return;
	}
	g_hidstick_mgr = IOHIDManagerCreate(kCFAllocatorDefault,
							kIOHIDOptionsTypeNone);
	if(!g_hidstick_mgr) {
		return;
	}
	dicts[0] = hidstick_match(kHIDUsage_GD_Joystick);
	dicts[1] = hidstick_match(kHIDUsage_GD_GamePad);
	dicts[2] = hidstick_match(kHIDUsage_GD_MultiAxisController);
	matches = CFArrayCreate(kCFAllocatorDefault, (const void **)dicts, 3,
						&kCFTypeArrayCallBacks);
	IOHIDManagerSetDeviceMatchingMultiple(g_hidstick_mgr, matches);
	CFRelease(matches);
	for(i = 0; i < 3; i++) {
		CFRelease(dicts[i]);
	}
	IOHIDManagerRegisterDeviceMatchingCallback(g_hidstick_mgr,
							hidstick_added, NULL);
	IOHIDManagerRegisterDeviceRemovalCallback(g_hidstick_mgr,
							hidstick_removed, NULL);
	IOHIDManagerRegisterInputValueCallback(g_hidstick_mgr,
							hidstick_value, NULL);
	g_hidstick_debug = (getenv("EMUBUDDY_JOYDEBUG") != NULL);
	IOHIDManagerScheduleWithRunLoop(g_hidstick_mgr, CFRunLoopGetMain(),
							kCFRunLoopCommonModes);
	if(IOHIDManagerOpen(g_hidstick_mgr, kIOHIDOptionsTypeNone) !=
							kIOReturnSuccess) {
		printf("USB joystick: cannot open the HID manager\n");
	}
	hidstick_adopt_any();
}

const char *
mac_hidstick_name(void)
{
	return g_hidstick_dev ? g_hidstick_name : NULL;
}

/* The hat switch at the last mac_hidstick_read (1 up 2 right 4 down 8 left) */
int
mac_hidstick_dpad(void)
{
	return g_hidstick_dev ? g_hidstick_dpad : 0;
}

int
mac_hidstick_present(void)
{
	return g_hidstick_dev != NULL;
}

static int
hidstick_get(IOHIDElementRef el, long *val_ptr)
{
	IOHIDValueRef val;

	val = NULL;
	if(!el || !g_hidstick_dev) {
		return 0;
	}
	if((IOHIDDeviceGetValue(g_hidstick_dev, el, &val) !=
					kIOReturnSuccess) || !val) {
		return 0;
	}
	*val_ptr = (long)IOHIDValueGetIntegerValue(val);
	return 1;
}

/* An axis as -32768 to 32767: what the stick reported last (has_rep), or
 * else what it says when asked */
static int
hidstick_axis(IOHIDElementRef el, int has_rep, long rep)
{
	long	val, min, max;

	min = (long)IOHIDElementGetLogicalMin(el);
	max = (long)IOHIDElementGetLogicalMax(el);
	if(max <= min) {
		return 0;
	}
	if(has_rep) {
		val = rep;
	} else if(!hidstick_get(el, &val)) {
		return 0;
	}
	if(val < min) {
		val = min;
	}
	if(val > max) {
		val = max;
	}
	return (int)(((val - min) * 65535) / (max - min)) - 32768;
}

/* The stick now: *x_ptr, *y_ptr -32768 to 32767, *buttons_ptr bit 0 and
 * bit 1 the two IIgs buttons. Returns 0 (and writes nothing) when there
 * is no stick. */
int
mac_hidstick_read(int *x_ptr, int *y_ptr, int *buttons_ptr)
{
	long	val, min, max, dir;
	int	x, y, buttons;
	int	i;

	if(!g_hidstick_dev) {
		return 0;
	}
	x = hidstick_axis(g_hidstick_x, g_hidstick_has_x, g_hidstick_rep_x);
	y = hidstick_axis(g_hidstick_y, g_hidstick_has_y, g_hidstick_rep_y);
	if(g_hidstick_hat && g_hidstick_has_hat) {
		val = g_hidstick_rep_hat;
		i = 1;
	} else {
		i = g_hidstick_hat && hidstick_get(g_hidstick_hat, &val);
	}
	if(i) {
		/* 8 (or 4) positions clockwise from up; anything else: at rest.
		 * It moves the stick all the way, like a gamepad's D-pad */
		min = (long)IOHIDElementGetLogicalMin(g_hidstick_hat);
		max = (long)IOHIDElementGetLogicalMax(g_hidstick_hat);
		dir = -1;
		if((val >= min) && (val <= max)) {
			if((max - min) == 7) {
				dir = val - min;
			} else if((max - min) == 3) {
				dir = (val - min) * 2;
			}
		}
		g_hidstick_dpad = 0;
		if((dir == 7) || (dir == 0) || (dir == 1)) {
			g_hidstick_dpad |= 1;
		} else if((dir >= 3) && (dir <= 5)) {
			g_hidstick_dpad |= 4;
		}
		if((dir >= 1) && (dir <= 3)) {
			g_hidstick_dpad |= 2;
		} else if((dir >= 5) && (dir <= 7)) {
			g_hidstick_dpad |= 8;
		}
		if((dir == 7) || (dir == 0) || (dir == 1)) {
			y = -32768;
		} else if((dir >= 3) && (dir <= 5)) {
			y = 32767;
		}
		if((dir >= 1) && (dir <= 3)) {
			x = 32767;
		} else if((dir >= 5) && (dir <= 7)) {
			x = -32768;
		}
	}
	buttons = 0;
	for(i = 0; i < HIDSTICK_MAX_BTN; i++) {
		if(!g_hidstick_btn[i]) {
			continue;
		}
		if(g_hidstick_has_btn & (1U << i)) {
			val = (g_hidstick_rep_btn >> i) & 1;
		} else if(!hidstick_get(g_hidstick_btn[i], &val)) {
			continue;
		}
		if(val != 0) {
			buttons |= 1 << (i & 1);
		}
	}
	*x_ptr = x;
	*y_ptr = y;
	*buttons_ptr = buttons;
	return 1;
}
