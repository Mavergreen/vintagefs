#!/bin/sh
# platform: host-agnostic -- cross-builds for Mac OS X 10.9 (x86_64) on a modern Mac or Linux, natively on 10.9
# usage: build.sh [lib|plugin]...   (default: lib); out of tree, into BUILD_DIR (default $MAVERICKS_BUILD_ROOT/vintagefs-109)
#        lib    BUILD_DIR/libvimg.a
#        plugin BUILD_DIR/VintageImages.plugin (VF_TRACE=1: every slot call logged to syslog)
set -eu
R="$(cd "$(dirname "$0")/.." && pwd)"
MAVERICKS_ROOT="$R"; export MAVERICKS_ROOT
. "$R/build/lib.sh"
B="${BUILD_DIR:-$MAVERICKS_BUILD_ROOT/vintagefs-109}"; mkdir -p "$B/obj"
CL="$(sh "$SHIPYARD_SCRIPTS/fetch_clang22.sh" "$R/components/clang22" 2>/dev/null)"
SDK="$(sh "$SHIPYARD_SCRIPTS/fetch_sdk.sh")"
CC="$CL/bin/clang --no-default-config --target=x86_64-apple-macos10.9 -isysroot $SDK"
AR="$CL/bin/llvm-ar"
lib() {
  objs=""
  for s in "$R"/src/*.c; do
    o="$B/obj/$(basename "$s" .c).o"
    $CC -std=c11 -D_DARWIN_C_SOURCE -O2 -g -Wall -Wextra -Werror -Wno-unused-parameter -c "$s" -o "$o"; objs="$objs $o"
  done
  rm -f "$B/libvimg.a"
  # shellcheck disable=SC2086
  $AR rcs "$B/libvimg.a" $objs
}
# platform: CFPlugIn knows a factory by its UUID, so a new one would make 10.9 see a different plug-in
FACTORY=3308498B-E96C-463C-AA2F-0DC88DBD36D8
plugin() {
  lib
  P="$B/VintageImages.plugin/Contents"; rm -rf "$B/VintageImages.plugin"; mkdir -p "$P/MacOS"
  trace=; [ "${VF_TRACE:-0}" = 1 ] && trace=-DVF_TRACE
  # shellcheck disable=SC2086
  $CC -std=c11 -D_DARWIN_C_SOURCE -O2 -g -Wall -Wextra -Werror -Wno-unused-parameter -fvisibility=hidden -fuse-ld=lld -bundle -Wl,-exported_symbol,_VintageImagesFactory \
    $trace -DVF_FACTORY_UUID="\"$FACTORY\"" -I"$R/src" "$R/src/plugin/plugin.c" "$B/libvimg.a" \
    -framework CoreFoundation -o "$P/MacOS/VintageImages"
  # platform: clang runs dsymutil when one -g invocation compiles and links, writing the dSYM beside the binary;
  #           it stays in BUILD_DIR for debugging and out of the bundle
  rm -rf "$B/VintageImages.dSYM"; if [ -d "$P/MacOS/VintageImages.dSYM" ]; then mv "$P/MacOS/VintageImages.dSYM" "$B/"; fi
  v="$(sh "$R/build/version.sh" 2>/dev/null || echo 0)"
  sed -e "s/@VERSION@/$v/g" -e "s/@FACTORY@/$FACTORY/g" "$R/src/plugin/Info.plist.in" > "$P/Info.plist"
}
[ $# -gt 0 ] || set -- lib
for a in "$@"; do case $a in lib) lib ;; plugin) plugin ;; *) echo "usage: build.sh [lib|plugin]..." >&2; exit 2 ;; esac; done
