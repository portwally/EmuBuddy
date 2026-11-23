/* EmuBuddy Launcher, macOS only: make the EmuBuddy it just started the
 * front application, so the game gets the keys and the joystick without a
 * click, and macOS does not slow it down as a background application (that
 * made games stutter). Since macOS 14 an application may only take the
 * focus when the front one gives it, so the Launcher, still in front,
 * activates the new process as soon as it is running. This runs on its own
 * thread: once the game covers the screen, the Launcher's main loop hardly
 * runs (drawing to a hidden window waits). */
#import <AppKit/AppKit.h>
#include <SDL3/SDL.h>

void
launcher_mac_front(SDL_Process *proc)
{
	pid_t	pid = (pid_t)SDL_GetNumberProperty(SDL_GetProcessProperties(proc),
					SDL_PROP_PROCESS_PID_NUMBER, 0);

	if(pid <= 0) {
		return;
	}
	dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
		int	i;

		for(i = 0; i < 100; i++) {		/* up to 10 seconds */
			@autoreleasepool {
				NSRunningApplication *app = [NSRunningApplication
					runningApplicationWithProcessIdentifier:pid];

				if(app && app.active) {
					return;
				}
				if(app) {
					[app activateWithOptions:
						NSApplicationActivateAllWindows];
				}
			}
			usleep(100000);
		}
	});
}
