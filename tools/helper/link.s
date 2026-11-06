* Merlin 32 link file: EmuBuddyHelper, a permanent init file (PIF, $B6) for
* SYSTEM/SYSTEM.SETUP. First spike: does it run at boot?
            DSK   EmuBuddyHelper
            TYP   $B6
            XPL
            ASM   main.s
            SNA   Main
            KND   #$1000
            ALI   BANK
            LNA   EmuBuddyHelper
