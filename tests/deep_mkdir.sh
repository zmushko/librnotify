#!/bin/sh
# Race-free recursive watching.
#
# Create a deep directory tree as fast as the shell can, then drop a
# handful of files at the leaf. With a naive inotifywait -r the
# subdirectories that materialise between mkdir(parent) and
# inotify_add_watch(parent) get no events for entries already inside.
# librnotify's killer feature is closing that race via readdir-after-
# add_watch and synthetic IN_CREATE.

. "$(dirname "$0")/lib.sh"

echo "== deep_mkdir =="
FAILED=0
TMP=$(mktemp -d)
trap 'stop_reporter; rm -rf "$TMP"' EXIT

mkdir "$TMP/watch"
start_reporter "$TMP/watch"

# Build the tree in a single shell command so all the mkdirs happen
# back-to-back. The deeper levels exist BEFORE inotify_add_watch had a
# chance to install a watch on their parents — exactly the race.
mkdir -p "$TMP/watch/a/b/c/d/e"
for i in 1 2 3 4 5; do
    printf 'x' >"$TMP/watch/a/b/c/d/e/f$i.txt"
done

drain

# Every directory along the path must surface.
assert_event "$TMP/watch/a"             "IN_CREATE on a"
assert_event "$TMP/watch/a/b"           "IN_CREATE on a/b"
assert_event "$TMP/watch/a/b/c"         "IN_CREATE on a/b/c"
assert_event "$TMP/watch/a/b/c/d"       "IN_CREATE on a/b/c/d"
assert_event "$TMP/watch/a/b/c/d/e"     "IN_CREATE on a/b/c/d/e"

# And every file created in the leaf — these are the ones that would
# be lost by a naive recursive watcher.
for i in 1 2 3 4 5; do
    assert_event "$TMP/watch/a/b/c/d/e/f$i.txt" "IN_CREATE on f$i.txt"
done

exit $FAILED
