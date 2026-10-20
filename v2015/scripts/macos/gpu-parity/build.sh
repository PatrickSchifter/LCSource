#!/bin/sh
# Builds the Metal x Vulkan parity test (parity.mm): the same scenes and the game's programs
# (programs/: the Direct3D assembly the engine makes, from LC_DUMP_PROGRAMS) drawn by both
# devices, read back and compared. Run: ./parity programs [-save <dir>] (it opens a small window);
# python3 sheet.py <dir> out.png <scene>... puts saved frames side by side.
# LC_VK_VALIDATION=1 with VK_LAYER_PATH and DYLD_FALLBACK_LIBRARY_PATH as in play.sh validates.
set -e
cd "$(dirname "$0")"
SRC="../../../Client Source"
G="$SRC/Engine/Graphics"
clang++ -std=c++17 -O1 -g -w -fobjc-arc -Iinc -I"$G" -I"$SRC" -I/opt/homebrew/include \
  "$G/ShaderAsmToARB.cpp" "$G/ShaderARBTranslate.cpp" -x objective-c++ "$G/MetalDevice.mm" "$G/VulkanDevice.mm" parity.mm \
  -L/opt/homebrew/lib -lSDL3 -lvulkan -lglslang -lglslang-default-resource-limits \
  -framework Metal -framework QuartzCore -framework AppKit -framework Foundation -o parity
