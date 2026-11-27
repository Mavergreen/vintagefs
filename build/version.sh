#!/bin/sh
# platform: host-agnostic
# spec: own upstream -- the version is YYYYMMDD.N from shipyard's self-upstream-version.sh, never -mavericks.N
set -eu
SELF="$(cd "$(dirname "$0")" && pwd)"
MAVERICKS_ROOT="$(cd "$SELF/.." && pwd)"; export MAVERICKS_ROOT
. "$SELF/msc.sh"
exec sh "$SHIPYARD_SCRIPTS/self-upstream-version.sh" --root "$MAVERICKS_ROOT" "$@"
