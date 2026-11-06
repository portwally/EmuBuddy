#!/bin/bash
# Assembles the EmuBuddy Helper (a GS/OS permanent init, $B6) with Merlin 32.
# The copy engine is shared with Receive (../receive/engine.s).
#   MERLIN32_DIR  the Merlin32 folder (default: ~/Downloads/Merlin32_v1.2_b2)
#   usage: ./build.sh
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
M="${MERLIN32_DIR:-$HOME/Downloads/Merlin32_v1.2_b2}"
case "$(uname -s)" in Darwin) BIN="$M/MacOs/Merlin32";; Linux) BIN="$M/Linux/Merlin32";; *) BIN="$M/Windows/Merlin32.exe";; esac
B="$HERE/build"; rm -rf "$B"; mkdir -p "$B"
cp "$HERE"/link.s "$HERE"/main.s "$HERE/../receive/engine.s" "$B/"
( cd "$B" && "$BIN" -V "$M/Library" link.s > merlin.log 2>&1 || { tail -20 merlin.log; exit 1; } )
grep -qi "error" "$B/EmuBuddyHelper_S01_Main_Output.txt" && { echo "assembler errors, see $B"; exit 1; }
echo "built $B/EmuBuddyHelper ($(wc -c < "$B/EmuBuddyHelper") bytes)"
