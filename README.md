# gsplus
Cross-platform Apple IIgs emulator and tools based on KEGS

## Downloads

Pre-built apps for macOS, Linux, and Windows are attached to each
[release](../../releases). The builds are not yet code-signed, so the OS may
warn that the app is from an unidentified developer the first time you run it.
This is expected — here's how to get past it:

### Windows
Windows Defender SmartScreen blocks downloaded, unsigned executables. To run it:

- **If you see "Windows protected your PC":** click **More info → Run anyway**.
- **If the `.exe` won't start after unzipping:** right-click `gsplus-sdl.exe`,
  choose **Properties**, tick **Unblock** at the bottom, then **OK**. (This
  clears the "mark of the web" that Windows adds to downloaded files.) Keep
  `SDL3.dll` next to the `.exe`.

Code signing to remove these prompts is planned but not yet set up.

### macOS
The `.app` inside the `.dmg` is only ad-hoc signed, so Gatekeeper will say it's
from an unidentified developer. To run it: **right-click the app → Open**, then
confirm **Open** in the dialog (you only need to do this once). If macOS still
refuses, run `xattr -dr com.apple.quarantine /Applications/GSplus-SDL.app`.

### Linux
The `.tar.gz` currently relies on a system SDL3 (`libsdl3` via your package
manager). A self-contained AppImage is planned.



### About Branches
- KEGS latest is tracked in `./upstream` directory
- There is an upstream branch that is updated whenever there are new version and merged to main.
- This makes it easy to track kegs changes somewhat independently of the gsplus work. 
