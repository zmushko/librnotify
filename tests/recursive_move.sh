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

# The initial recursive scan emits synthetic CREATE/CLOSE_WRITE for
# every pre-existing entry — including .../src/sub/file.txt. Those are
# correct (the path was indeed under src/ at dispatch time) and would
# poison a "no event under src/" assertion further down. Drain the
# initial scan first, then clip the log so the rest of the test only
# sees events emitted after the rename.
drain
baseline=$(wc -l <"$EVENTS_LOG")

# Move the whole subtree under a new name within the same watch root.
mv "$TMP/watch/src" "$TMP/watch/dst"
drain

# Trigger an event INSIDE the renamed subtree. With renameWatches doing
# its job, the path should appear under the NEW prefix.
echo "new" >"$TMP/watch/dst/sub/file.txt"
drain

# Keep only post-baseline lines.
tail -n "+$((baseline + 1))" "$EVENTS_LOG" >"${EVENTS_LOG}.post"
mv "${EVENTS_LOG}.post" "$EVENTS_LOG"

assert_event    "MOVED_FROM"                   "MOVED_FROM emitted for src"
assert_event    "MOVED_TO"                     "MOVED_TO emitted for dst"
assert_event    "$TMP/watch/dst/sub/file.txt"  "event surfaces under new path"
assert_no_event "$TMP/watch/src/sub/file.txt"  "no event under stale path"

exit $FAILED
