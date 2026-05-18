#!/bin/sh
# Move a watched subdirectory inside the watch root. The kernel keeps
# the wds attached to the moved subtree alive, but the paths
# librnotify cached for them go stale. renameWatches() fixes that up:
# subsequent events inside the moved tree must surface under the NEW
# path, not the old one.

. "$(dirname "$0")/lib.sh"

echo "== recursive_move =="
FAILED=0
TMP=$(mktemp -d)
trap 'stop_reporter; rm -rf "$TMP"' EXIT

mkdir -p "$TMP/watch/src/sub"
echo "old" >"$TMP/watch/src/sub/file.txt"

start_reporter "$TMP/watch"

# Move the whole subtree under a new name within the same watch root.
mv "$TMP/watch/src" "$TMP/watch/dst"
drain

assert_event "MOVED_FROM" "MOVED_FROM emitted for src"
assert_event "MOVED_TO"   "MOVED_TO emitted for dst"

# Trigger an event INSIDE the renamed subtree. With renameWatches doing
# its job, the path should appear under the NEW prefix.
echo "new" >"$TMP/watch/dst/sub/file.txt"
drain

assert_event    "$TMP/watch/dst/sub/file.txt"  "event surfaces under new path"
assert_no_event "$TMP/watch/src/sub/file.txt"  "no event under stale path"

exit $FAILED
