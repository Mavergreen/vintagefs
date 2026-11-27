# platform: host-agnostic
: "${MAVERICKS_ROOT:=$(cd "$(dirname "${BASH_SOURCE:-$0}")/.." 2>/dev/null && pwd || pwd)}"
export MAVERICKS_ROOT
. "$MAVERICKS_ROOT/build/msc.sh"
. "$SHIPYARD/lib.sh"
# platform: a family checkout may live on NFS; builds go to local disk
: "${MAVERICKS_BUILD_ROOT:=${TMPDIR:-/tmp}/mm-build}"
export MAVERICKS_BUILD_ROOT
VF_TREE="usr/local/mavergreen/vintagefs"
VF_PLUGIN="Library/Plug-ins/DiskImages/VintageImages.plugin"
