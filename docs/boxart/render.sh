#!/bin/sh
# render.sh: the box's faces from box.html, flat, with headless Chrome:
# front.png, back.png, spine.png, top.png. Apple Garamond
# must be installed (box.html takes it from the system's fonts).
# The homepage shows them as a box in 3D (EmuBuddy/img/box/).
cd "$(dirname "$0")" || exit 1
CHROME="/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
shoot() {   # $1 face, $2 width, $3 height
  "$CHROME" --headless --disable-gpu --hide-scrollbars --force-device-scale-factor=1 --allow-file-access-from-files \
    --window-size="$2,$3" --virtual-time-budget=4000 --screenshot="$PWD/$1.png" "file://$PWD/box.html?$1" 2>/dev/null
}
shoot front 900 1180
shoot back 900 1180
shoot spine 170 1180
shoot top 900 170
ls -la front.png back.png spine.png top.png
