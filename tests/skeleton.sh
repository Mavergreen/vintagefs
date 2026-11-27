#!/bin/sh
# platform: host-agnostic
set -eu
R="$(cd "$(dirname "$0")/.." && pwd)"
fail=0
. "$R/build/msc.sh" 2>/dev/null || { echo "build/msc.sh missing or unsourceable" >&2; exit 1; }
cmp -s "$R/build/msc.sh" "$SHIPYARD_SCRIPTS/templates/msc.sh" || { echo "build/msc.sh differs from shipyard's template" >&2; fail=1; }
v="$(sh "$R/build/version.sh" 2>/dev/null || :)"
echo "$v" | grep -Eq '^[0-9]{8}\.[0-9]+$' || { echo "build/version.sh printed '$v', want YYYYMMDD.N" >&2; fail=1; }
# platform: dash, Ubuntu's sh, gives a sourced file the caller's $0, so build/lib.sh cannot find the repo by itself;
#           each script that sources it names the repo first
for f in $(grep -rl '^\. "\$R/build/lib.sh"' "$R/build" "$R/tests"); do
  grep -B1 '^\. "\$R/build/lib.sh"' "$f" | head -n 1 | grep -q '^MAVERICKS_ROOT="\$R"; export MAVERICKS_ROOT$' ||
    { echo "${f#"$R/"} sources build/lib.sh without setting MAVERICKS_ROOT=\"\$R\" on the line before" >&2; fail=1; }
done
[ "$fail" -eq 0 ] && echo "skeleton OK"
exit "$fail"
