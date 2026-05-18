#!/bin/sh
# Atomic-save pattern: write a temp file, then rename(2) it over the
# target. We should see CLOSE_WRITE on the temp and a MOVED_FROM /
# MOVED_TO pair on the rename with matching cookie values.

. "$(dirname "$0")/lib.sh"

echo "== atomic_save =="
FAILED=0
TMP=$(mktemp -d)
trap 'stop_reporter; rm -rf "$TMP"' EXIT

mkdir "$TMP/watch"
start_reporter "$TMP/watch"

printf 'content' >"$TMP/watch/.target.swp"
sync
mv "$TMP/watch/.target.swp" "$TMP/watch/target"

drain

assert_event "CLOSE_WRITE" "CLOSE_WRITE arrived for the temp file"
assert_event "MOVED_FROM"  "MOVED_FROM arrived"
assert_event "MOVED_TO"    "MOVED_TO arrived"

# Cookie pairing: the cookie field on MOVED_FROM and MOVED_TO must
# match for the rename pair. Format is "EVENT FLAGS COOKIE PATH".
from_cookie=$(awk '/MOVED_FROM/ {print $3; exit}' "$EVENTS_LOG")
to_cookie=$(awk   '/MOVED_TO/   {print $3; exit}' "$EVENTS_LOG")

if [ -z "$from_cookie" ] || [ -z "$to_cookie" ]; then
    echo "  FAIL  cookie pair extraction (FROM=$from_cookie TO=$to_cookie)"
    FAILED=$((FAILED + 1))
elif [ "$from_cookie" != "$to_cookie" ]; then
    echo "  FAIL  cookies differ: FROM=$from_cookie TO=$to_cookie"
    FAILED=$((FAILED + 1))
elif [ "$from_cookie" = "0" ]; then
    echo "  FAIL  cookie was zero (rename pair must have nonzero cookie)"
    FAILED=$((FAILED + 1))
else
    echo "  PASS  MOVED_FROM / MOVED_TO share cookie $from_cookie"
fi

exit $FAILED
