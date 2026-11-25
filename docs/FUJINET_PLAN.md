# FujiNet: plan

FujiNet is a network and storage device for old computers. On the Apple II it is a chain of SmartPort devices: eight
disk drives, a network device, a clock, a printer, a modem, a CP/M device and "THE_FUJI", the device that configures the
rest. Programs written with `fujinet-lib` find these devices by name and talk to them with ordinary SmartPort calls.

This plan lets EmuBuddy's IIgs use a FujiNet. EmuBuddy does not emulate the FujiNet itself: the FujiNet project has
**FujiNet-PC**, its real firmware built as a program for macOS, Linux and Windows. EmuBuddy passes every SmartPort call
for a FujiNet device to that program over a local network connection, with the project's own protocol for this,
**SmartPort over SLIP**. An AppleWin fork does the same for the Apple //e.

All facts below were read from the sources on 2026-11-24: `FujiNetWIFI/fujinet-firmware` (the PC build, `lib/devrelay`,
`lib/bus/iwm`) and `markjfisher/AppleWin` (`source/SPoverSLIP`, `source/devrelay`). Nothing has been built or run yet.

## 1. Decisions

| Question | Decision |
|---|---|
| Where do the FujiNet devices show up? | **As extra units of the Hard Disk Card in slot 7**, after the seven hard disks (units 8 and up). This card already takes every SmartPort call in the emulator (`smartport.c`, `do_c70d`), the long calls of GS/OS included, so no new slot, no new ROM and no new trap are needed. A card of its own in another slot is a later option (phase 6). |
| Who emulates the FujiNet? | FujiNet-PC, a separate program the user starts. EmuBuddy only passes calls on. |
| Connection | As AppleWin does it: **EmuBuddy listens** on TCP port 1985 and FujiNet-PC connects (its `[BOIP]` setting, `localhost:1985` by default). EmuBuddy listens on this computer only (127.0.0.1), not on the network. |
| Code | Written new, in C, from the protocol. The FujiNet code is C++ and GPL-3.0 (the AppleWin side GPL-2.0 or later); EmuBuddy is GPLv3, so reading it is fine, but nothing is copied. |
| Slot ROM | None. The AppleWin card has a ROM with a start-up routine taken from another project; EmuBuddy does not need it. |
| With the High-Speed SCSI Card | Not available at first: that card is not a SmartPort card. Phase 6 would lift this. |
| Default | Off. A setting turns it on. |

## 2. How SmartPort over SLIP works

- **Frames.** Each request and each answer is one SLIP frame: `C0`, the bytes, `C0`. Inside, `C0` is sent as `DB DC` and
  `DB` as `DB DD`. There is no length and no checksum; the frame's length is the length.
- **Request:** 11 bytes, then data.

  | Byte | Content |
  |---|---|
  | 0 | sequence number, counts up, wraps at 256 |
  | 1 | command: 0 STATUS, 1 READ BLOCK, 2 WRITE BLOCK, 3 FORMAT, 4 CONTROL, 5 INIT, 6 OPEN, 7 CLOSE, 8 READ, 9 WRITE |
  | 2 | parameter count |
  | 3 | unit, as numbered on this connection |
  | 4–5 | zero |
  | 6–10 | STATUS and CONTROL: the code, then the network unit. READ/WRITE BLOCK: the block number, 3 bytes, low byte first. READ/WRITE: byte count (2 bytes), address (3 bytes). |
  | 11… | WRITE BLOCK: 512 bytes. WRITE: the data. CONTROL: the control list **with** its 2 length bytes. |

- **Answer:** byte 0 the sequence number again, byte 1 the SmartPort error (0 = fine), then data only when it is 0:
  the status list or DIB, the 512 bytes of a block, or the bytes read.
- **Start of a connection.** No greeting. EmuBuddy sends INIT to unit 1, 2, 3 … until an answer is not 0: that unit is
  the last one (`$FF`, end of chain) and counts too. After that the units are numbered and stay so until the connection
  ends. A second round of INITs gets no answer at all, so the count is taken once per connection.
- **The devices,** in the order FujiNet-PC gives them (measured in phase 0): eight disks, then CPM (if turned on),
  FN_CLOCK, NETWORK, THE_FUJI, PRINTER and MODEM. 14 units.
- **End of a connection.** FujiNet-PC tries to connect again every 100 ms, for ever. EmuBuddy forgets the units and
  counts them again at the next connection.
- **Two places where the wiki and the code differ** (the code wins): STATUS and CONTROL carry the network unit in byte 7,
  and the CONTROL data includes the list's two length bytes.

## 3. What exists and what is missing

| Part | State today |
|---|---|
| SmartPort calls, short and long | Exist in `smartport.c` (`do_c70d`) for units 1–7 with disk images. They return "no device" for other units. |
| ProDOS block calls of slot 7 | Exist (`do_c70a`), for drives 1 and 2. |
| Units 8 and up | The card says it has 16 units (`MAX_C7_DISKS`); 8–16 are empty. FujiNet needs up to 14, so 21 in all: the count has to follow what is connected. |
| Calls a disk card never gets | **Missing:** OPEN, CLOSE, READ, WRITE (the character devices), and STATUS/CONTROL codes other than the disk ones. FujiNet programs use them all the time. |
| TCP listener | Missing here. EmuBuddy has socket code to build on (`scc_socket_driver.c`, `webgate.c`). |
| SLIP | Missing. About 60 lines. |
| A place in the windows | Missing: on/off, the port, and whether a FujiNet is connected. |

## 4. Phases

Each phase ends in something that can be tested, and the Hard Disk Card keeps working after every phase.

### Phase 0: a running FujiNet-PC (small)
- Get FujiNet-PC for the Apple II: the project's nightly build for macOS (`fujinet-pc-APPLE_…_macos-arm64.tar.gz`), or
  build it (`./build.sh -cp APPLE` in `fujinet-firmware`; needs cmake below version 4 and MbedTLS 3).
- Start it (`./run-fujinet`), open its web page (`http://localhost:8000`), and look at `fnconfig.ini`.
- Write a 50-line test program that listens on port 1985, sends the INITs and a STATUS to each unit, and prints the
  answers. This shows the real device list and DIBs before EmuBuddy is touched.
- Get test software: the FujiNet configuration program for the Apple II, and one `fujinet-lib` program that uses the
  network device.

**Done (2026-11-25).**
- FujiNet-PC: the nightly build `fujinet-pc-APPLE_v1.6.2-dev_macos-15-arm64.tar.gz` (2.6 MB, from the releases of
  `FujiNetWIFI/fujinet-firmware`) runs on this Mac as it comes. It needs Homebrew's `openssl@4`. Started with
  `./fujinet -u http://127.0.0.1:8000`, so its web page is open to this computer only (the default is every network
  interface).
- `tools/fujinet_listen.py` plays the emulator's side: it listens on 127.0.0.1:1985, sends the INITs and asks every
  unit for its status and DIB. FujiNet-PC connected within a second.
- **The device list is the other way round from what the source reading said: the disks come first.**

  | Unit | Name | Type | Notes |
  |---|---|---|---|
  | 1–8 | FUJINET_DISK_48 … _55 | `$01` with a disk, `$02` empty | unit 1 holds a disk of 280 blocks at once |
  | 9 | CPM | `$12` | |
  | 10 | FN_CLOCK | `$13` | |
  | 11 | NETWORK | `$11` | |
  | 12 | THE_FUJI | `$10` | |
  | 13 | PRINTER | `$15` | |
  | 14 | MODEM | `$15` | answers INIT with `$FF`: the end of the chain |

- **Disk 1 is FujiNet's own configuration program.** FujiNet-PC puts `data/autorun.po` (140 KB, ProDOS) into its
  first disk when it starts. Six blocks read over the connection were the same as that file; a block past the end gave
  error `$27`. The package also has `data/mount-and-boot.po`. So the test software for phase 2 comes with FujiNet-PC.
- Confirmed: a second round of INITs gets no answer. An answer with an error can still carry 512 bytes of data, so
  EmuBuddy must look at the error byte first.
- What this means for the plan:
  - A real Apple II starts up from FujiNet's disk 1 into the configuration program. Behind seven hard disks that does
    not happen. For phase 2 the same program can be run from `autorun.po` put into a 5.25" drive; whether it then finds
    THE_FUJI at a unit above 7 is the first thing to try.
  - "FujiNet units first" (risks, below) is worth more than thought: with FujiNet's units in front, ProDOS 8 sees its
    disks as slot 7, drives 1 and 2, and the IIgs can start from them.
- Not looked at: what FujiNet-PC wants on a reset of the Apple II, and a `fujinet-lib` program for the network device.

### Phase 1: the connection (small to medium)
- `fujinet.c`, with nothing of the IIgs in it: SLIP coding, the listener on 127.0.0.1, one connection at a time, the INIT
  round, and `fujinet_call(command, unit, parameters, data out, data in)`, which sends a request and waits for its
  answer (10 seconds at most, as AppleWin; answers with an old sequence number are thrown away).
- Test: `tests/test_fujinet.c`, built like `test_scsi`. It plays FujiNet-PC on a local socket: frames with the special
  bytes in them, the INIT round, a block read, a timeout, a dropped connection.

**Done (2026-11-25).**
- `fujinet.c` / `fujinet.h`: `fujinet_start(port)`, `fujinet_poll()`, `fujinet_units()`, `fujinet_call(...)` and
  `fujinet_stop()`. No threads: `fujinet_poll()` takes a connection and counts its units, and a call waits for its
  answer with `select`. It builds for Windows too (Winsock), but only the macOS build was run.
- It is compiled into EmuBuddy but nothing calls it yet: the IIgs does not see a FujiNet before phase 2.
- `tests/test_fujinet.c` (`make test_fujinet`): a thread plays FujiNet-PC. It covers: no connection; counting the units
  up to the `$FF` answer; STATUS with its parameters; a block with the two bytes SLIP has to change, sent in two pieces;
  an error answer with data behind it; WRITE BLOCK and CONTROL with data; an answer that comes too late and must not
  be taken for the next call's; FujiNet-PC going away in the middle of a call; and coming back. All pass.
- `test_fujinet real` waits for a real FujiNet-PC: it found the 14 units of phase 0 with the same names and types.
- Not tested: the Windows and Linux builds of `fujinet.c`, and two FujiNet-PCs trying to connect at once (the second
  one waits).

### Phase 2: the units on the Hard Disk Card (medium)
- `smartport.c`: units above 7 go to `fujinet.c`, with unit 8 as FujiNet's unit 1. Short and long calls. EmuBuddy
  copies the data between the IIgs's memory and the request.
  - STATUS for unit 0 reports 7 plus the number of FujiNet units.
  - STATUS, CONTROL, READ BLOCK, WRITE BLOCK, FORMAT, INIT, OPEN, CLOSE, READ, WRITE.
  - The network unit that `fujinet-lib` puts into its parameter list goes into byte 7.
- Without a connection, units above 7 answer "no device", as today.
- A reset of the IIgs: find out in phase 0 what FujiNet-PC expects (the wiki says a CONTROL with code 0 to each
  device; AppleWin sends nothing).
- Goal: under ProDOS 8, the FujiNet configuration program finds THE_FUJI and lists its hosts.

**Done (2026-11-25).**
- `smartport.c`: with a FujiNet connected, SmartPort units 8 and up are FujiNet's units 1 and up
  (`smartport_fujinet_call`), for the short and the long calls. STATUS for unit 0 then reports 7 plus FujiNet's units;
  without a FujiNet it reports 16 as before. INIT for a FujiNet unit is answered by EmuBuddy itself, because
  FujiNet-PC answers no INIT after it has connected.
- Settings `g_fujinet` (0 off, 1 on; default off) and `g_fujinet_port` (1985), so far only in the config and as
  `-g_fujinet 1` on the command line. `smartport_fujinet_tick()` runs once a frame: it starts and stops the listener
  as the setting says and takes the connection.
- **FujiNet's configuration program runs.** `CONFIG.SYSTEM` from FujiNet-PC's `autorun.po`, on a ProDOS 8 disk made for
  the test, shows FujiNet's host list (SD, fujinet.online, …) and its eight drives. FujiNet-PC's log shows the calls
  arriving at THE_FUJI: GET WIFI STATUS, READ HOST SLOTS, READ DEVICE SLOTS. `autorun.po` itself does not start from
  a 5.25" drive: it has no ProDOS on it and a start-up block of its own.
- **GS/OS starts with the FujiNet connected** (232 STATUS calls went to its units) and shows FujiNet's first disk,
  CONFIG, as a volume in the Finder. So the long SmartPort calls and a block device behind FujiNet work.
- With the setting off, and with it on but no FujiNet-PC running, Total Replay and GS/OS start as before; the
  High-Speed SCSI Card too.
- Seen on the way: when two FujiNet-PCs run, the second waits and takes over when the first ends.
- Not tested: writing to a FujiNet disk, the calls OPEN, CLOSE, READ and WRITE (the network device), CONTROL calls with
  data from a real program, a reset of the IIgs while connected, and what GS/OS makes of the units that are not disks
  (it started and showed nothing odd, but nothing used them).

### Phase 3: software that uses it (medium)
- ProDOS 8: mount a disk image through FujiNet (a TNFS server) and read it; a `fujinet-lib` network program fetches a
  web page.
- GS/OS: does it start with 14 more SmartPort units, what does it make of the units that are not disks, and does a
  FujiNet disk show up in the Finder? Nobody has run FujiNet on an emulated IIgs yet; this is where surprises are
  likely.
- The clock device and the printer: only that they answer.

### Phase 4: settings and windows (small to medium)
- Setting "FujiNet" (off, on) and its port, in the config and in the Slots window, next to the Hard Disk Card.
- The status bar or the Drives window says whether a FujiNet is connected and how many units it has.
- CLI: `-fujinet on|off`, `-fujinetport <n>`. Launcher: a profile key.

### Phase 5: documents (small)
- README, `docs/CLI.md`, the manual: what FujiNet is, where FujiNet-PC comes from, how to start both, where its web
  page is.

### Phase 6 (later, optional)
- EmuBuddy starts FujiNet-PC itself and stops it at the end, as it does with the web gateway. FujiNet-PC is GPL-3.0,
  so it may be shipped with EmuBuddy; it is a second program to build for three systems.
- A FujiNet card of its own in another slot, with a small ROM written for EmuBuddy: FujiNet together with the
  High-Speed SCSI Card, and starting up from a FujiNet disk.

## 5. Size

| Phase | New or changed lines | Sessions |
|---|---|---|
| 0 Reference | ~50 (test program) | ½ |
| 1 Connection | ~350 with its test | 1 |
| 2 Units on the card | ~250 | 1 |
| 3 Software | fixes as they come | 1–2 |
| 4 Settings | ~150 | ½ |
| 5 Documents | – | ½ |
| **Total** | **~800** | **4–6** |

## 6. Risks and open questions

- **The IIgs is untested ground.** AppleWin has no IIgs, and its card handles only the short SmartPort calls in 64 KB of
  memory. GS/OS and its long calls with FujiNet are new.
- **The emulator waits.** A SmartPort call stops the IIgs until FujiNet-PC answers. A slow network (a web request, a
  TNFS server far away) freezes the IIgs for that time, up to the 10-second limit. AppleWin has the same.
- **Many units.** 21 SmartPort units on one card is more than ProDOS 8 can give drive numbers to; it only takes the
  first ones as disks. FujiNet's disks are its units 1 to 8, but behind the seven hard disks they are units 8 to 15, so
  ProDOS 8 will not see them as drives without help. FujiNet programs find their devices by name and are not affected. A setting "FujiNet
  units first" may be needed.
- **Starting up from a FujiNet disk** is not possible in this design (phase 6).
- **The protocol may change.** It has no version number. The firmware's side was last changed in November 2026; the
  AppleWin card is not in AppleWin proper and its last work is from 2025.
- **Port 1985 open to this computer only.** Another program on the same computer could still connect and pretend to
  be a FujiNet. That is the same trust as for the web gateway.
- **Not checked:** the serial variant of the protocol, what FujiNet-PC does on an Apple II reset, and whether the
  nightly macOS build runs on this Mac.
