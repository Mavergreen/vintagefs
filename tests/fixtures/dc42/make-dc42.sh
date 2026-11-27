#!/bin/sh
# platform: host-agnostic -- needs hfsutils (hformat, hmount, hcopy) and python3; makes the DiskCopy 4.2 fixtures --
#           classic HFS floppies, one 800K, one 800K with tag bytes, one whose stored checksum is wrong -- and the raw
#           800K volume they wrap, which 10.9 attaches without the plug-in
# usage: make-dc42.sh OUT
set -eu
H="$(cd "$(dirname "$0")" && pwd)"; OUT="$1"; mkdir -p "$OUT"
W="$(mktemp -d "${TMPDIR:-/tmp}/make-dc42.XXXXXX")"; trap 'rm -rf "$W"; humount 2>/dev/null || :' EXIT
export HOME="$W"  # platform: hfsutils keeps its current-volume state in $HOME/.hcwd
printf 'classic hfs check\n' > "$W/hello.txt"
k=800
dd if=/dev/zero of="$W/raw$k" bs=1024 count=$k 2>/dev/null
hformat -l CLASSIC "$W/raw$k" >/dev/null
hmount "$W/raw$k" >/dev/null; hcopy -r "$W/hello.txt" :hello.txt; humount
cp "$W/raw800" "$OUT/raw800k.hfs"
python3 -I "$H/mkdc42.py" "$W/raw800" "$OUT/classic-800k.image" CLASSIC 1 0
python3 -I "$H/mkdc42.py" "$W/raw800" "$OUT/classic-800k-tags.image" CLASSIC 1 12
python3 -I "$H/mkdc42.py" "$W/raw800" "$OUT/badsum-800k.image" CLASSIC 1 0 badsum
ls -l "$OUT"
