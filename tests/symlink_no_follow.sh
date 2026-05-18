#!/bin/sh
# Symlink no-follow: with IN_DONT_FOLLOW always set on inotify_add_watch
# and lstat in addNotify, a symlink planted inside the watched tree
# must not steer the recursive descent into the link target. A change
# inside the link target (which lives OUTSIDE the watched tree) must
# produce no event.

. "$(dirname "$0")/lib.sh"

echo "== symlink_no_follow =="
FAILED=0
TMP=$(mktemp -d)
trap 'stop_reporter; rm -rf "$TMP"' EXIT

mkdir "$TMP/watch"
mkdir "$TMP/outside"
echo "secret" >"$TMP/outside/secret.txt"

# Plant the symlink BEFORE starting the reporter so it shows up during
# the initial readdir-after-add-watch pass — the same code path that
# would have followed it before #8/#10.
ln -s "$TMP/outside" "$TMP/watch/link"

start_reporter "$TMP/watch"

# Modify content INSIDE the symlink target. If the recursive descent
# followed the link, we would see IN_MODIFY / IN_CLOSE_WRITE on
# $TMP/outside/secret.txt. We must not.
echo "changed" >"$TMP/outside/secret.txt"

# Also: touch a regular file inside watch to confirm reporter is alive
echo "alive" >"$TMP/watch/canary.txt"

drain

assert_event    "$TMP/watch/canary.txt"        "canary file event arrived"
assert_no_event "$TMP/outside/secret.txt"      "no event leaked from symlink target"
assert_no_event "$TMP/outside/"                "no event under outside/ at all"

# The symlink itself may show up as a CREATE in the watch root, but
# without ISDIR (because we used lstat, not stat). Sanity-check that
# the link does NOT report as a directory.
if grep -F "$TMP/watch/link" "$EVENTS_LOG" | grep -q "ISDIR"; then
    echo "  FAIL  symlink reported with ISDIR"
    FAILED=$((FAILED + 1))
else
    echo "  PASS  symlink not reported as ISDIR"
fi

exit $FAILED
