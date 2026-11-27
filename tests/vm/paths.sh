#!/bin/sh
# platform: macOS-only -- a macOS host with the 10.9 VM at `ssh -p 2222 localhost`: every way into the plug-in on 10.9
# usage: paths.sh   (VM_SSH overrides the ssh command; needs passwordless sudo on the VM for the plug-in folder;
#                    DMG_CODECS_PKG names a dmg-codecs pkg, else gh fetches its latest release)
set -eu
R="$(cd "$(dirname "$0")/../.." && pwd)"
MAVERICKS_ROOT="$R"; export MAVERICKS_ROOT
. "$R/build/lib.sh"
B="${BUILD_DIR:-$MAVERICKS_BUILD_ROOT/vintagefs-109}"
VM_SSH="${VM_SSH:-ssh -p 2222 localhost}"
W=/var/tmp/vintagefs-paths
BUILD_DIR="$B" VF_TRACE="${VF_TRACE:-0}" sh "$R/build/build.sh" plugin
T="$(mktemp -d "${TMPDIR:-/tmp}/vf-paths.XXXXXX")"; trap 'rm -rf "$T"' EXIT
mkdir -p "$T/a"; cp -R "$B/VintageImages.plugin" "$T/a/"
cp "$R"/tests/fixtures/dc42/*.image "$R"/tests/fixtures/dc42/*.hfs "$R"/tests/fixtures/ndif/*.data "$R"/tests/fixtures/ndif/*.rsrc \
  "$R/tests/vm/paths-vm.sh" "$T/a/"
# spec: tests/vm/paths-vm.sh -- vintagefs and dmg-codecs installed together, each opening its own images
pkg="${DMG_CODECS_PKG:-}"
if [ -z "$pkg" ]; then gh release download -R Mavergreen/dmg-codecs --pattern '*.pkg' -D "$T/dc" >/dev/null && pkg="$(ls "$T"/dc/*.pkg)"; fi
pkgutil --expand-full "$pkg" "$T/dcx" >/dev/null
cp -R "$(find "$T/dcx" -name DMGCodecs.plugin -type d | head -n 1)" "$T/a/"
mkdir "$T/u"; echo "a file in a ULFO image" > "$T/u/ulfo.txt"
hdiutil create -quiet -fs HFS+ -volname "VF ULFO" -srcfolder "$T/u" -format ULFO "$T/a/ulfo.dmg"
# platform: 10.9's tar refuses pax headers and xattrs
( cd "$T" && COPYFILE_DISABLE=1 tar --format ustar --no-xattrs --no-mac-metadata -cf - a ) |
  $VM_SSH "rm -rf $W && mkdir -p $W && tar -xf - -C $W && cd $W/a && sh paths-vm.sh; rc=\$?; rm -rf $W; exit \$rc"
