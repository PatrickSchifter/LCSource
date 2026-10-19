#!/bin/bash
# Copies the macOS client build (build-client-mac/Bin) into the game install's Bin/ and starts it,
# as scripts/windows/deploy.sh + jogar.bat do on Windows. The game directory is the parent of Bin/.
#   scripts/macos/play.sh [extra arguments]
# LC_GAME_DIR: the install (default ~/Projects/LC-BR2014); LC_BUILD_DIR: default build-client-mac.
# Output goes to $LC_GAME_DIR/stdout.log; the engine writes Console.log there too.
set -e
cd "$(dirname "$0")/../.."
GAME=${LC_GAME_DIR:-$HOME/Projects/LC-BR2014}
BUILD=${LC_BUILD_DIR:-build-client-mac}
[[ -d $GAME/Data ]] || { echo "no game data in $GAME" >&2; exit 1; }
pkill -9 -f "$GAME/Bin/Nksp" 2>/dev/null || true
mkdir -p "$GAME/Bin"
rsync -a --exclude ecc "$BUILD/Bin/" "$GAME/Bin/"
cd "$GAME"
# renders with Metal; LC_GFX_API=opengl renders with OpenGL instead
# shaders run on the GPU; LC_NO_HW_SHADERS=1 goes back to skinning on the CPU
exec "$GAME/Bin/Nksp" 'fkzktlfgod!' "$@" > "$GAME/stdout.log" 2>&1
