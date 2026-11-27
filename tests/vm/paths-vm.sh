#!/bin/sh
# platform: macOS-only -- runs on Mac OS X 10.9, from tests/vm/paths.sh's staging folder: every way into the plug-in
# usage: sh paths-vm.sh   (cwd holds VintageImages.plugin, DMGCodecs.plugin and the fixtures; scratch goes to ../out)
set -u
W="$(cd .. && pwd)"; P=/Library/Plug-ins/DiskImages; O="$W/out"; mkdir -p "$O"
fail=0
ok() { echo "ok: $*"; }
no() { echo "FAIL: $*"; fail=1; }
ours() { hdiutil info | awk -v w="$W" '/^image-path/ {p = index($0, w) > 0} p && /^\/dev\/disk[0-9]+[ \t]/ {print $1}'; }
detach_all() { for d in $(ours); do hdiutil detach -quiet -force "$d"; done; }
cleanup() { detach_all; sudo -n rm -rf "$P/VintageImages.plugin" "$P/DMGCodecs.plugin"; }
trap cleanup EXIT
install() { sudo -n mkdir -p "$P" && sudo -n cp -R "$1" "$P/" && sudo -n chown -R root:wheel "$P/$1" || exit 1; }
crashes() { ls "$HOME/Library/Logs/DiagnosticReports" /Library/Logs/DiagnosticReports 2>/dev/null | grep -c -E '^(diskimages-helper|hdiutil|DiskImageMounter)'; }
norm() { sed -E -e 's#/dev/disk[0-9]+(s[0-9]+)?#/dev/diskN#g' -e "s#$W#W#g"; }
sum() { md5 -q "$1"; }
dev_of() { hdiutil attach -nomount "$@" 2>&1 | awk '/^\/dev\/disk/ {print $1; exit}'; }
# writes MARK into sector 1500 of the attached image's raw device: 0 if the device took it
poke() { printf 'VINTAGEFS-WRITE-TEST' | dd of="/dev/r${1#/dev/}" bs=512 seek=1500 conv=sync 2>/dev/null; }
finder_info() { xattr -wx com.apple.FinderInfo "$(printf '%s' "$2" | xxd -p)000000000000000000000000000000000000000000000000" "$1"; }
attach_msg() { hdiutil attach -nobrowse -readonly -nomount "$1" 2>&1 | tail -n 1 | norm; }

for f in RdWr Rdxx ROCo Rken; do
  cp $f.data $f.img; cat $f.rsrc > $f.img/..namedfork/rsrc
  case $f in RdWr) finder_info $f.img dimgddsk ;; *) finder_info $f.img rohdddsk ;; esac
done
cp RdWr.data datafork-only.img
hdiutil create -quiet -size 1m -fs HFS+ -volname "VF HFSP" -layout NONE "$O/hfsplus.dmg"
hdiutil convert -quiet "$O/hfsplus.dmg" -format UDZO -o "$O/udzo.dmg"
declined="datafork-only.img Rken.img raw800k.hfs $O/hfsplus.dmg $O/udzo.dmg"

sudo -n rm -rf "$P/VintageImages.plugin" "$P/DMGCodecs.plugin" || exit 1
for i in $declined; do eval "without_$(basename "$i" | tr -c 'a-zA-Z0-9\n' _)=\"\$(attach_msg \"\$i\")\""; detach_all; done
install VintageImages.plugin
base_crashes=$(crashes)

for i in classic-800k.image classic-800k-tags.image; do
  out=$(hdiutil attach -nobrowse -readonly -mountpoint "$W/m" "$i" 2>&1)
  if [ $? -eq 0 ] && grep -qx "classic hfs check" "$W/m/hello.txt"; then ok "$i mounts through 10.9's hfs.fs and reads"; else no "$i: $out"; fi
  detach_all
done
out=$(hdiutil imageinfo classic-800k.image 2>&1)
if echo "$out" | grep -qx "Format: DC42" && echo "$out" | grep -qx "Format Description: Disk Copy 4.2"; then ok "imageinfo: DC42, Disk Copy 4.2"
else no "imageinfo DC42: $(echo "$out" | head -n 3 | tr '\n' ' ')"; fi
out=$(hdiutil verify classic-800k.image 2>&1) && ok "verify classic-800k.image" || no "verify classic-800k.image: $out"
out=$(hdiutil verify badsum-800k.image 2>&1) && no "verify badsum-800k.image passed: $out" || ok "verify badsum-800k.image fails: $(echo "$out" | tail -n 1)"

if [ "$(stat -f %Su /dev/console)" = "$(id -un)" ]; then
  # platform: 10.9's DiskImageMounter claims Disk Copy's type codes, not the .image extension
  cp classic-800k.image "$O/double-click.image"; finder_info "$O/double-click.image" dImgdCpy; open "$O/double-click.image"; n=0
  while [ $n -lt 30 ] && ! [ -f /Volumes/CLASSIC/hello.txt ]; do sleep 1; n=$((n + 1)); done
  if grep -qx "classic hfs check" /Volumes/CLASSIC/hello.txt 2>/dev/null; then ok "open (double-click) mounts it at /Volumes/CLASSIC"; else no "open did not mount it in 30 s"; fi
  detach_all
else echo "skip: double-click needs the VM's console user logged in"; fi

for i in classic-800k.image classic-800k-tags.image; do
  cp "$i" "$O/w.image"; before=$(sum "$O/w.image"); tags=$(tail -c +$((84 + 819200 + 1)) "$O/w.image" | md5 -q)
  d=$(dev_of "$O/w.image")
  if [ -z "$d" ]; then no "$i: read-write attach failed"; continue; fi
  poke "$d" && wrote=1 || wrote=0
  hdiutil detach -quiet "$d"
  if [ $wrote = 1 ] && [ "$(sum "$O/w.image")" != "$before" ] && dd if="$O/w.image" bs=1 skip=$((84 + 1500 * 512)) count=20 2>/dev/null | grep -q VINTAGEFS-WRITE-TEST
  then ok "$i: a raw write lands in the image"; else no "$i: the raw write did not land (wrote=$wrote)"; fi
  out=$(hdiutil verify "$O/w.image" 2>&1) && ok "$i: verify passes after the write" || no "$i: verify after write: $out"
  [ "$(tail -c +$((84 + 819200 + 1)) "$O/w.image" | md5 -q)" = "$tags" ] && ok "$i: tag bytes untouched" || no "$i: tag bytes changed"
done

cp badsum-800k.image "$O/b.image"; cp classic-800k.image "$O/l.image"; chflags uchg "$O/l.image"
# platform: 10.9 verifies a format's checksum before attaching, as it does a UDIF's, so a damaged image needs -noverify
out=$(hdiutil attach -nomount "$O/b.image" 2>&1) && { no "badsum attached without -noverify: $out"; detach_all; } || ok "badsum refused at attach: $(echo "$out" | tail -n 1)"
for i in "$O/b.image" "$O/l.image"; do
  before=$(sum "$i"); d=$(dev_of -noverify "$i")
  if [ -z "$d" ]; then no "$(basename "$i"): did not attach"; continue; fi
  poke "$d" && no "$(basename "$i"): a write was accepted" || ok "$(basename "$i") attaches read-only"
  hdiutil detach -quiet "$d"; [ "$(sum "$i")" = "$before" ] || no "$(basename "$i") changed"
done
chflags nouchg "$O/l.image"

for f in "RdWr NDIF read/write" "Rdxx NDIF read-only" "ROCo NDIF compressed"; do
  set -- $f; n=$1; shift
  out=$(hdiutil attach -nobrowse -readonly -mountpoint "$W/m" $n.img 2>&1)
  if [ $? -eq 0 ] && grep -q "vintagefs fixture line 11999" "$W/m/text.txt"; then ok "$n mounts and reads"; else no "$n: $out"; fi
  detach_all
  out=$(hdiutil imageinfo $n.img 2>&1)
  if echo "$out" | grep -qx "Format: $n" && echo "$out" | grep -qx "Format Description: $*"; then ok "imageinfo: $n, $*"
  else no "imageinfo $n: $(echo "$out" | head -n 3 | tr '\n' ' ')"; fi
  before=$(sum $n.img); d=$(dev_of $n.img)
  if [ -n "$d" ]; then poke "$d" && no "$n accepted a write" || ok "$n attaches read-only"; hdiutil detach -quiet "$d"; fi
  [ "$(sum $n.img)" = "$before" ] || no "$n changed"
done

for i in $declined; do
  v=without_$(basename "$i" | tr -c 'a-zA-Z0-9\n' _); eval "w=\$$v"; got=$(attach_msg "$i"); detach_all
  [ "$got" = "$w" ] && ok "$(basename "$i") as without the plug-in: $got" || no "$(basename "$i"): without [$w] with [$got]"
done

for i in ROCo.img classic-800k.image; do
  cp -p "$i" "$O/s-$i"; [ $i = ROCo.img ] && cat ROCo.rsrc > "$O/s-$i/..namedfork/rsrc" && finder_info "$O/s-$i" rohdddsk
  before=$(sum "$O/s-$i"); d=$(dev_of -shadow "$O/s-$i.shadow" "$O/s-$i")
  if [ -z "$d" ]; then no "$i: -shadow attach failed"; continue; fi
  poke "$d" && wrote=1 || wrote=0
  hdiutil detach -quiet "$d"
  if [ $wrote = 1 ] && [ "$(sum "$O/s-$i")" = "$before" ] && [ -s "$O/s-$i.shadow" ]; then ok "$i: -shadow takes writes and leaves the image alone"
  else no "$i: -shadow (wrote=$wrote)"; fi
done

install DMGCodecs.plugin
a=$(hdiutil attach -nobrowse -readonly -mountpoint "$W/mu" ulfo.dmg 2>&1); b=$(hdiutil attach -nobrowse -readonly -mountpoint "$W/mc" classic-800k.image 2>&1)
if [ -f "$W/mu/ulfo.txt" ] && grep -qx "classic hfs check" "$W/mc/hello.txt" 2>/dev/null; then ok "with dmg-codecs installed too, a ULFO and a DC42 attach at once"
else no "with dmg-codecs: [$a] [$b]"; fi
detach_all

[ "$(crashes)" = "$base_crashes" ] || no "hdiutil, the helper or DiskImageMounter crashed"
exit $fail
