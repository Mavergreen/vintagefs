#!/bin/sh
# platform: host-agnostic -- clang22 and the pinned SDK build the updater on Linux and on a Mac alike
#   usage: build-updater.sh     prints the built vintagefs-updater.app (x86_64, 10.9)
set -eu
R="$(cd "$(dirname "$0")/.." && pwd)"
MAVERICKS_ROOT="$R"; export MAVERICKS_ROOT
. "$R/build/lib.sh"
B="${BUILD_DIR:-$MAVERICKS_BUILD_ROOT/vintagefs-109}/updater"
CL="$(sh "$SHIPYARD_SCRIPTS/fetch_clang22.sh" "$R/components/clang22")"
# platform: /usr/local/mavergreen/bin reaches PATH only through a login shell's paths.d; on Linux, install@v1
#           installs no shipyard-cmake and points find_package at its checkout instead
SYCM="$(command -v shipyard-cmake || { [ -x /usr/local/mavergreen/bin/shipyard-cmake ] && echo /usr/local/mavergreen/bin/shipyard-cmake; } || echo cmake)"
"$SYCM" -S "$R" -B "$B" -DCMAKE_TOOLCHAIN_FILE="$(dirname "$SHIPYARD_SCRIPTS")/MavericksToolchain.cmake" \
  -DCMAKE_OSX_ARCHITECTURES=x86_64 -DCMAKE_OBJC_COMPILER="$CL/bin/clang" \
  -DCMAKE_OBJC_COMPILER_TARGET=x86_64-apple-macos10.9 -DCMAKE_OBJC_FLAGS=--no-default-config \
  -DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld >&2
"$SYCM" --build "$B" >&2
app="$(find "$B" -maxdepth 3 -name vintagefs-updater.app -type d | head -1)"
[ -n "$app" ] || { echo "build-updater: no vintagefs-updater.app under $B" >&2; exit 1; }
echo "$app"
