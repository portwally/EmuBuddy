# EmuBuddy host bridge

The bridge lets a program running inside the emulated IIgs read and write files in **one folder of the host computer**
(the *bridge folder*). It is off until the user chooses a folder (Disks > Bridge Folder..., or `-bridge <folder>`), and
the program can reach nothing outside that folder.

A program such as a file manager can use it to copy files and folders between any GS/OS volume and the host.

## Calling it

The call is the CPU instruction **`WDM $EB,$00`** (the bytes `42 EB 00`). On a real IIgs WDM does nothing, so a program
finds out whether the bridge exists by presetting the status word to `$FFFF` and checking it afterwards.

- Native mode, 16-bit accumulator.
- **DB:A** is the address of a *parameter block* (A = 16-bit offset, DB = bank).
- The command runs at once, and the result is written back into the block. Registers are not changed.

### Parameter block

All values are little-endian. A pointer is 4 bytes (the top byte is zero). Offsets in hex:

| Offset | Size | Field |
|---|---|---|
| 00 | word | command |
| 02 | word | **status** (output): 0 = ok, otherwise a GS/OS error code; preset to `$FFFF` to detect a missing bridge |
| 04 | long | p1 (input) |
| 08 | long | p2 |
| 0C | long | p3 |
| 10 | long | p4 |
| 14 | long | r1 (output) |
| 18 | long | r2 |
| 1C | long | r3 |
| 20 | long | r4 |

### Paths

A path is a pointer to a **word length followed by that many characters**. Names are separated by `/` or `:`;
the path is relative to the bridge folder (a leading separator is ignored, an empty path is the folder itself).
Names must not contain `.` `..` as a component, `\`, control characters, `*` or `?`; such paths give error `$40`.
Names are matched case-insensitively. Symbolic links that lead out of the bridge folder are refused.

### File names, type and aux type

The host stores the ProDOS file type and aux type in the file name, exactly as shared folders do:

| Host name | Meaning |
|---|---|
| `NAME,t$B3,a$DB03` | type $B3, aux $DB03 (`,t$..` is left out for BIN $06, `,a$..` for aux 0) |
| `NAME.txt` | text, type $04 |
| `NAME` | BIN, type $06, aux 0 |
| `NAME#B3DB03` | the Cadius form is read as well |

The bridge returns and takes the **ProDOS name without the suffix**. Folders have type $0F. Only the data fork is
handled; resource forks are not supported yet.

## Commands

| Command | p1 | p2 | p3 | p4 | Results |
|---|---|---|---|---|---|
| `$0000` INFO | buffer for the folder name (or 0) | buffer size | | | r1 = version (1), r2 = largest transfer (32768); status `$2F` if no folder is set (r1, r2 are still valid) |
| `$0010` LIST | path of a folder | output buffer | buffer size | first entry (0-based) | r1 = entries returned, r2 = 1 if more follow |
| `$0011` MKDIR | path | | | | |
| `$0012` DELETE | path | | | | file or **empty** folder |
| `$0013` GETINFO | path | | | | r1 = 1 if folder, r2 = size, r3 = type, r4 = aux |
| `$0020` OPEN | path | mode: 1 read, 2 write (create or replace) | type (write) | aux (write) | r1 = reference (1-8), r2 = size, r3 = type, r4 = aux (read) |
| `$0021` READ | reference | buffer | count (at most 32768) | | r1 = bytes read (0 at the end) |
| `$0022` WRITE | reference | buffer | count (at most 32768) | | r1 = bytes written |
| `$0023` CLOSE | reference | | | | |

A LIST entry is: word entry length (the whole entry), byte flags (bit 0 = folder), byte type, word aux, long size,
word name length, name characters. Entries are sorted by name, so `first entry` can be used to continue a long listing.
Up to 8 files can be open at once; they are closed by a reset.

### Errors

| Code | Meaning |
|---|---|
| `$27` | I/O error |
| `$2F` | no bridge folder chosen (device not connected) |
| `$40` | bad path |
| `$43` | bad file reference |
| `$44` | path not found |
| `$46` | file not found |
| `$47` | duplicate (the folder exists already) |
| `$48` | disk full / short write |
| `$4E` | access refused (symbolic link out of the folder, or the folder is also mounted as a disk) |
| `$53` | bad parameter |
| `$54` | out of references |

## Rules for the host side

- The folder must not also be mounted as a disk (Disks > Share a Folder): both would write to the same files, so the
  bridge answers `$4E` while they overlap, and the folder chooser refuses such a folder.
- Memory is read and written through the emulator's own memory access, so banks and shadowing behave as the program
  expects.

## Trying it

`tools/bridgetest/` is a small GS/OS program (Merlin 32) that creates `fromiigs/` in the bridge folder, writes a text
file and a typed file, and records every status code. `tools/bridgetest/build.sh` assembles it. The host side is
tested by `cmake --build build --target test_bridge && build/test_bridge`.

## Dropping files into GS/OS folders (Receive)

A shared folder can only be reached through its own disk. To drop a file from your computer onto a Finder window that shows
a folder on **another** disk (for example `:Boot.Disk:Dragtests`), EmuBuddy and a small IIgs program work together:

1. You drop the file on the emulator while that Finder window is in front, and a bridge folder is set. EmuBuddy does not
   write into the disk. It **stages** the file in the bridge folder instead:
   - `Inbox/NNNN/NAME,t$TT,a$AAAA` is the file. `NAME` is made ProDOS-safe (at most 15 characters, a letter first, letters,
     digits and dots) and type and aux type come from the extension (`.txt` text, `.shk` $E0/$8002, `.bin`, `.bas`,
     `.system`; everything else BIN).
   - `Inbox/NNNN/target.txt` holds the GS/OS path of the folder (`:Boot.Disk:Dragtests`) and a CR. It is written last, so a
     drop that is still being copied is skipped.
   - Files with a resource fork (`.applesingle`) are not staged.
2. In the IIgs the **EmuBuddy helper** picks the drop up by itself. It is a permanent init
   (`SYSTEM/SYSTEM.SETUP/EMUBUDDYHELPER`, type $B6) that checks `Inbox` about once a second from the heartbeat
   interrupt, using the Scheduler so GS/OS calls are safe. EmuBuddy installs it once, after asking, into the startup disk
   (S7D1) while the disk is not in use, then restarts the IIgs. The bridge folder is made automatically
   (`EmuBuddy/Bridge` in the application data folder) unless you chose one or switched the bridge off. The same copy
   engine is also available as the program **Receive** (`tools/receive`), which you run by hand
   (Disks > Copy Receive to Shared Folder). Either one lists `Inbox`, and for every drop with a target it creates the file in that folder (data fork, with type and aux
   type), copies the data in 16 KB pieces, removes the staged files, and leaves a note in `Outbox/NNNN.txt`.
3. EmuBuddy reads the notes once a second and shows them in the status bar: `OK :Boot.Disk:Dragtests:NAME`, or
   `ERR $xx path`.

Errors keep the staged files in `Inbox` so nothing is lost: `$47` the name exists already, `$44` the folder does not exist
(or was closed), `$48` the disk is full, `$4E` write protected. Receive reports them again the next time it runs. Delete the
`Inbox/NNNN` folder on the computer to forget a drop.

The helper and Receive need nothing else: it only uses the bridge and GS/OS. Its source is in `tools/receive/` (Merlin 32); the helper is
in `tools/helper/` and shares `engine.s`.
