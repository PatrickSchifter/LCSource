#!/bin/bash
# Installs the Homebrew packages needed to build the Last Chaos port on macOS.
set -euo pipefail

if ! command -v brew >/dev/null; then
  echo "Homebrew is required: https://brew.sh" >&2
  exit 1
fi

# Build tools
brew install cmake ninja pkg-config

# Server libraries
brew install boost mysql-client log4cxx apr apr-util expat jsoncpp botan openssl@3

# Client libraries (platform layer)
brew install sdl3

cat <<MSG

Done. Configure and build with:
  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DOPENSSL_ROOT_DIR=\$(brew --prefix openssl@3)
  ninja -C build
MSG
