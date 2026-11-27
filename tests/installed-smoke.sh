#!/bin/sh
# platform: macOS-only -- runs ON Mac OS X 10.9 (release.yml's vm-test guest, or the maintainer's VM): installer, hdiutil, sudo
#   usage: installed-smoke.sh <PKG> <VERSION>
#          installs PKG, proves it is VERSION, attaches a DiskCopy 4.2 and a ROCo image through it, uninstalls it with
#          the mavergreen helper, and proves 10.9 then refuses the DiskCopy 4.2 image again. With no arguments, exits 77.
# spec: shipyard check 29 -- a pkg-shipping repo installs its run's own pkg on 10.9 and runs it before publish
set -eu
[ "$#" -eq 2 ] || { echo "installed-smoke: needs <PKG> <VERSION>; only the 10.9 acceptance run passes them; skipping"; exit 77; }
pkg=$1 ver=$2
F="$(cd "$(dirname "$0")" && pwd)/fixtures"
P=/Library/Plug-ins/DiskImages/VintageImages.plugin
# platform: the family's base component installs the mavergreen helper at /usr/local/bin, outside the prefix
helper=/usr/local/bin/mavergreen
fail() { echo "FAIL: $*"; exit 1; }
case "$(sw_vers -productVersion)" in 10.9|10.9.*) ;; *) fail "this is not Mac OS X 10.9" ;; esac
[ -f "$pkg" ] || fail "no $pkg"
if pkgutil --pkgs | grep -qx dev.mavergreen.vintagefs; then fail "this Mac already has a vintagefs receipt"; fi
W="$(mktemp -d /var/tmp/vf-smoke.XXXXXX)"
trap 'for m in "$W"/m-*; do [ -d "$m" ] && hdiutil detach -force "$m" >/dev/null 2>&1; done; rm -rf "$W"' EXIT
# platform: NDIF's block map lives in the resource fork, which a git checkout cannot carry; Rdxx and ROCo files are
#           Finder type rohd, creator ddsk
cp "$F/ndif/ROCo.data" "$W/roco.img"; cat "$F/ndif/ROCo.rsrc" > "$W/roco.img/..namedfork/rsrc"
xattr -wx com.apple.FinderInfo "$(printf rohdddsk | xxd -p)000000000000000000000000000000000000000000000000" "$W/roco.img"

sudo installer -pkg "$pkg" -target /
got="$(pkgutil --pkg-info dev.mavergreen.vintagefs | awk '$1 == "version:" { print $2 }')"
[ "$got" = "$ver" ] || fail "the installed dev.mavergreen.vintagefs is '$got', want $ver"
[ -d "$P" ] || fail "the pkg did not install $P"
"$helper" list | grep -q '^vintagefs ' || fail "mavergreen list does not show vintagefs"

hdiutil verify "$F/dc42/classic-800k.image" >/dev/null 2>&1 || fail "hdiutil verify classic-800k.image"
out="$(hdiutil attach -nobrowse -readonly -mountpoint "$W/m-dc42" "$F/dc42/classic-800k.image" 2>&1)" || fail "classic-800k.image does not attach: $out"
grep -qx "classic hfs check" "$W/m-dc42/hello.txt" || fail "classic-800k.image's hello.txt does not read back"
hdiutil detach "$W/m-dc42" >/dev/null || fail "classic-800k.image does not detach"
echo "ok: a DiskCopy 4.2 image verifies, attaches and reads"
out="$(hdiutil attach -nobrowse -readonly -mountpoint "$W/m-roco" "$W/roco.img" 2>&1)" || fail "the ROCo image does not attach: $out"
grep -q "vintagefs fixture line 11999" "$W/m-roco/text.txt" || fail "the ROCo image's text.txt does not read back"
hdiutil detach "$W/m-roco" >/dev/null || fail "the ROCo image does not detach"
echo "ok: a ROCo image attaches and reads"

sudo "$helper" uninstall vintagefs
[ ! -e "$P" ] || fail "uninstall left $P"
if pkgutil --pkgs | grep -qx dev.mavergreen.vintagefs; then fail "uninstall left the receipt"; fi
if hdiutil attach -nobrowse -readonly -mountpoint "$W/m-after" "$F/dc42/classic-800k.image" >/dev/null 2>&1; then
  fail "classic-800k.image still attaches after uninstall"
fi
echo "PASS: installed smoke ($ver)"
