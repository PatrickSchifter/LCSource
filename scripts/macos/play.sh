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
# renders with Metal; LC_GFX_API=opengl renders with OpenGL instead, LC_GFX_API=vulkan with Vulkan
# (MoltenVK); LC_VK_VALIDATION=1 turns on Vulkan's validation layer (errors in stdout.log)
if [[ ${LC_VK_VALIDATION:-0} != 0 ]]; then
  VVL=$(brew --prefix vulkan-validationlayers 2>/dev/null)
  export VK_LAYER_PATH=$VVL/share/vulkan/explicit_layer.d DYLD_FALLBACK_LIBRARY_PATH=$VVL/lib VK_LAYER_VALIDATE_SYNC=true
fi
# shaders run on the GPU; LC_NO_HW_SHADERS=1 goes back to skinning on the CPU
exec "$GAME/Bin/Nksp" 'fkzktlfgod!' "$@" > "$GAME/stdout.log" 2>&1
