#!/bin/bash
# Copies the 2009 macOS client build (v2009/build-client/Bin) into the install's Bin/ and starts
# it, as scripts/macos/play.sh does for the 2015 client.
#   v2009/scripts/client-play.sh [extra arguments]
# LC_GAME_DIR: the install (default ~/Projects/LC-2009, made by client-install.sh).
# Output goes to $LC_GAME_DIR/stdout.log. LC_GFX_API=opengl|vulkan picks the renderer (Metal by
# default); LC_CONSOLE_LOG=1 writes the console log too.
set -e
cd "$(dirname "$0")/.."
GAME=${LC_GAME_DIR:-$HOME/Projects/LC-2009}
[[ -d $GAME/Data ]] || { echo "no game data in $GAME (run v2009/scripts/client-install.sh)" >&2; exit 1; }
pkill -9 -f "$GAME/Bin/Nksp" 2>/dev/null || true
mkdir -p "$GAME/Bin"
rsync -a --exclude ecc build-client/Bin/ "$GAME/Bin/"
cd "$GAME"
# '6574': the 2009 client's own switch to run without the launcher (no "dlwltnr" check) and
# without comparing its binaries against the publisher's
exec "$GAME/Bin/Nksp" 6574 "$@" > "$GAME/stdout.log" 2>&1
