#!/bin/sh
# platform: host-agnostic -- builds each tests/c/test_*.c against libvimg's sources with the host's clang22 and runs it
# usage: run.sh [TEST...]   (default: every test); BUILD_DIR defaults to $MAVERICKS_BUILD_ROOT/vintagefs-tests
#        VF_SANITIZE=1 (a Mac only) builds them with ASan and UBSan
set -eu
R="$(cd "$(dirname "$0")/../.." && pwd)"
MAVERICKS_ROOT="$R"; export MAVERICKS_ROOT
. "$R/build/lib.sh"
B="${BUILD_DIR:-$MAVERICKS_BUILD_ROOT/vintagefs-tests}"; mkdir -p "$B"
CL="$(sh "$SHIPYARD_SCRIPTS/fetch_clang22.sh" "$R/components/clang22" 2>/dev/null)"
case "$(uname -s):${VF_SANITIZE:-0}" in
  # platform: clang22-cross targets 10.9 by default; tests run natively on the Mac's own arch (no Rosetta).
  #           The test binaries never ship.
  Darwin:0) CC="$CL/bin/clang --no-default-config --target=arm64-apple-macos11 -isysroot $(sh "$SHIPYARD_SCRIPTS/fetch_sdk.sh" --arch arm64) -fuse-ld=lld" ;;
  # platform: clang22 ships no sanitizer runtimes on any host, so VF_SANITIZE=1 takes Apple's clang (INGREDIENTS.md)
  Darwin:1) CC="/usr/bin/clang -fsanitize=address,undefined -fno-sanitize-recover=undefined" ;;
  # platform: clang22's clang.cfg targets Darwin and its Linux archive carries no Linux compiler-rt, so a Linux test
  #           build sets its own target and links the system's libgcc (and its crt and libc) with lld
  *:0) CC="$CL/bin/clang --no-default-config --target=x86_64-linux-gnu -fuse-ld=lld --rtlib=libgcc --unwindlib=libgcc" ;;
  *) echo "run.sh: VF_SANITIZE=1 needs a Mac: clang22 ships no sanitizer runtimes" >&2; exit 2 ;;
esac
SRCS="$(ls "$R"/src/*.c 2>/dev/null || :)"
tests="$*"; [ -n "$tests" ] || tests="$(cd "$R/tests/c" && ls test_*.c | sed 's/\.c$//')"
fail=0
for t in $tests; do
  # shellcheck disable=SC2086
  if $CC -std=c11 -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE -g -O1 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-function \
       -I"$R/src" "$R/tests/c/$t.c" $SRCS -o "$B/$t" 2> "$B/$t.build.log"; then
    "$B/$t" "$R/tests/fixtures" || fail=1
  else
    cat "$B/$t.build.log" >&2; echo "FAIL: $t did not build" >&2; fail=1
  fi
done
exit $fail
