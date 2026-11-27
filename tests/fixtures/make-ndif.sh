#!/bin/sh
# platform: 10.9 only -- 10.9's hdiutil still writes NDIF, and reads it with legacy-disabled=false, so it makes the
#           fixtures and says what each should read as. Run on the 10.9 VM; copy OUT back from the developer's side.
# usage: make-ndif.sh OUT
set -eu
OUT="$1"; W="$(mktemp -d /var/tmp/make-ndif.XXXXXX)"; mkdir -p "$OUT"
hdiutil create -quiet -size 1200k -fs HFS+ -volname NDIF -type UDIF -layout NONE "$W/base.dmg"
mnt="$(hdiutil attach -nobrowse "$W/base.dmg" | awk 'END{print $NF}')"
# platform: a fixed seed, so a rerun writes the same files: half incompressible, half text, so ROCo has both kinds
perl -e 'srand(42); print map { chr(int(rand(256))) } 1..200000' > "$mnt/noise.bin"
perl -e 'print "vintagefs fixture line $_\n" for 1..12000' > "$mnt/text.txt"
hdiutil detach -quiet "$mnt"
: > "$OUT/expected.txt"
for f in RdWr Rdxx ROCo Rken; do
  hdiutil convert -quiet "$W/base.dmg" -format "$f" -o "$W/$f.img"
  cat "$W/$f.img" > "$OUT/$f.data"
  cat "$W/$f.img/..namedfork/rsrc" > "$OUT/$f.rsrc"
  if hdiutil convert -quiet -imagekey legacy-disabled=false "$W/$f.img" -format UDTO -o "$W/$f.cdr" 2>/dev/null; then
    echo "$f $(shasum -a 256 < "$W/$f.cdr" | cut -c1-64) $(stat -f %z "$W/$f.cdr")" >> "$OUT/expected.txt"
  else
    echo "$f unreadable" >> "$OUT/expected.txt"
  fi
done
rm -rf "$W"
cat "$OUT/expected.txt"
