#!/bin/sh
# platform: host-agnostic -- stevedore builds the pkg through shipyard's seams
#   usage: package-pkg.sh     builds the plug-in and its updater into dist/vintagefs-<version>.pkg
set -eu
R="$(cd "$(dirname "$0")/.." && pwd)"
MAVERICKS_ROOT="$R"; export MAVERICKS_ROOT
. "$R/build/lib.sh"
B="${BUILD_DIR:-$MAVERICKS_BUILD_ROOT/vintagefs-109}"
SHORT=vintagefs; IDENTIFIER="$(sh "$SHIPYARD_SCRIPTS/product-name.sh" identifier "$SHORT")"
VER="$(sh "$R/build/version.sh" 2>/dev/null)"
[ -n "$VER" ] || { echo "package-pkg: build/version.sh gave no version" >&2; exit 1; }
CL="$(sh "$SHIPYARD_SCRIPTS/fetch_clang22.sh" "$R/components/clang22" 2>/dev/null)"
LIPO="$CL/bin/llvm-lipo" OTOOL="$CL/bin/llvm-otool" NM="$CL/bin/llvm-nm" STRINGS="$CL/bin/llvm-strings"
export LIPO OTOOL NM STRINGS
BUILD_DIR="$B" sh "$R/build/build.sh" plugin >&2
MAVERICKS_DEVIATIONS_ROOT="$R" MAVERICKS_ALLOW_ARCHS=x86_64 \
  sh "$SHIPYARD_SCRIPTS/assert_binary_compatible.sh" "$B/VintageImages.plugin/Contents/MacOS/VintageImages" >&2
app="$(BUILD_DIR="$B" sh "$R/build/build-updater.sh")"

pr="$B/pkgroot"; scr="$B/pkg-scripts"; comp="$B/$SHORT-component.pkg"
rm -rf "$pr" "$scr" "$comp"
mkdir -p "$pr/$VF_TREE" "$pr/$(dirname "$VF_PLUGIN")" "$R/dist"
rm -f "$R"/dist/"$SHORT"-*.pkg
cp -R "$B/VintageImages.plugin" "$pr/$VF_PLUGIN"
cp "$R/README.md" "$R/THIRD-PARTY-NOTICES.txt" "$pr/$VF_TREE/"
sh "$SHIPYARD_SCRIPTS/stage_product.sh" --stage "$pr" --product "$SHORT" --name "vintagefs for Mavericks" \
  --version "$VER" --scripts-out "$scr" --updater-app "$app"
sh "$SHIPYARD_SCRIPTS/build_component_pkg.sh" --root "$pr" --identifier "$IDENTIFIER" --version "$VER" \
  --install-location / --scripts "$scr" --out "$comp" >/dev/null
pkg="$R/dist/$SHORT-$VER.pkg"
sh "$SHIPYARD_SCRIPTS/set_install_floor.sh" --identifier "$IDENTIFIER" --title "vintagefs for Mavericks $VER" \
  --component "$comp" --out "$pkg" --min-os 10.9.5 --host-arch x86_64 --require-scripts >/dev/null
rm -f "$comp"
sh "$SHIPYARD_SCRIPTS/build-info.sh" "$R/dist/build-info-native.txt" \
  variant=native arch=x86_64 pkg="$(basename "$pkg")" identifier="$IDENTIFIER"
echo "$pkg"
