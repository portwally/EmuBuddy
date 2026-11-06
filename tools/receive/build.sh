#!/bin/bash
# Assembles Receive (a GS/OS S16 program) with Merlin 32 and, if a folder is
# given, copies it there as Receive,t$B3,a$DB03 (the name a shared folder needs).
#   MERLIN32_DIR  the Merlin32 folder (default: ~/Downloads/Merlin32_v1.2_b2)
#   usage: ./build.sh [shared-folder]
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
M="${MERLIN32_DIR:-$HOME/Downloads/Merlin32_v1.2_b2}"
case "$(uname -s)" in Darwin) BIN="$M/MacOs/Merlin32";; Linux) BIN="$M/Linux/Merlin32";; *) BIN="$M/Windows/Merlin32.exe";; esac
B="$HERE/build"; rm -rf "$B"; mkdir -p "$B"
cp "$HERE"/link.s "$HERE"/main.s "$B/"
( cd "$B" && "$BIN" -V "$M/Library" link.s > merlin.log 2>&1 || { tail -20 merlin.log; exit 1; } )
grep -qi "error" "$B/Receive_S01_Main_Output.txt" && { echo "assembler errors, see $B"; exit 1; }
echo "built $B/Receive ($(wc -c < "$B/Receive") bytes)"
[ -n "$1" ] && cp "$B/Receive" "$1/Receive,t\$B3,a\$DB03" && echo "copied to $1"
exit 0
