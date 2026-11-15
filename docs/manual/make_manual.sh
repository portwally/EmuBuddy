#!/bin/bash
# Makes the user's guide, docs/EmuBuddy-Manual.pdf, from docs/manual/:
# make_manual.py writes build/manual/manual.html, Chrome prints it, the pages
# of the chapters, sections and index words are read from that PDF, and it is
# printed again with them (three times: the numbers can move the pages).
# Needs Apple Garamond installed, Google Chrome and poppler (pdftotext).
set -e
cd "$(dirname "$0")/../.."
ROOT="$PWD"
CHROME="/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
M="$ROOT/build/manual"; mkdir -p "$M"
print() { "$CHROME" --headless --disable-gpu --no-pdf-header-footer --allow-file-access-from-files \
    --virtual-time-budget=5000 --print-to-pdf="$1" "file://$M/manual.html" 2> /dev/null; }
pages() { python3 -I - "$1" "$M/pages.json" <<'PY'
import json, re, subprocess, sys
n = int(re.search(r"Pages:\s+(\d+)", subprocess.run(["pdfinfo", sys.argv[1]], capture_output=True, text=True).stdout).group(1))
marks, texts = {}, []
for p in range(1, n + 1):
    t = subprocess.run(["pdftotext", "-f", str(p), "-l", str(p), "-layout", sys.argv[1], "-"], capture_output=True, text=True).stdout
    for k in re.findall(r"@@([\w-]+)@@", t):
        marks.setdefault(k, p)
    texts.append(re.sub(r"@@[\w-]+@@", "", t))
json.dump({"marks": marks, "texts": texts}, open(sys.argv[2], "w"))
print(n, "pages")
PY
}
rm -f "$M/pages.json"
python3 -I docs/manual/make_manual.py > /dev/null
for pass in 1 2 3; do
    print "$M/pass.pdf"
    pages "$M/pass.pdf" > /dev/null
    PAGES="$M/pages.json" python3 -I docs/manual/make_manual.py > /dev/null
done
print "$ROOT/docs/EmuBuddy-Manual.pdf"
echo "docs/EmuBuddy-Manual.pdf: $(pdfinfo "$ROOT/docs/EmuBuddy-Manual.pdf" | awk '/Pages/{print $2}') pages"
