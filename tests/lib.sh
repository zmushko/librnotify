# Shared helpers for librnotify stress tests.
#
# Each test sources this file, sets FAILED=0, creates a temp dir, calls
# start_reporter, performs its operations, then makes a series of
# assert_event / assert_no_event calls. Exit status is the FAILED count.
#
# Usage idiom:
#     . "$(dirname "$0")/lib.sh"
#     FAILED=0
#     TMP=$(mktemp -d)
#     trap 'stop_reporter; rm -rf "$TMP"' EXIT
#     mkdir "$TMP/watch"
#     start_reporter "$TMP/watch"
#     ...do stuff...
#     drain
#     assert_event "..." "description"
#     exit $FAILED

set -eu

TESTS_DIR=$(cd -- "$(dirname -- "$0")" && pwd)
REPORTER=$TESTS_DIR/reporter
[ -x "$REPORTER" ] || { echo "build reporter first: cd .. && make check"; exit 2; }

start_reporter() {
    watchdir=$1
    EVENTS_LOG=$(mktemp)
    READY_LOG=$(mktemp)
    "$REPORTER" "$watchdir" >"$EVENTS_LOG" 2>"$READY_LOG" &
    REPORTER_PID=$!
    # Poll up to ~5s for the READY token.
    waited=0
    while [ $waited -lt 50 ]; do
        if grep -q "^READY" "$READY_LOG" 2>/dev/null; then
            return 0
        fi
        if ! kill -0 "$REPORTER_PID" 2>/dev/null; then
            echo "reporter died before READY:" >&2
            cat "$READY_LOG" >&2
            return 1
        fi
        sleep 0.1
        waited=$((waited + 1))
    done
    echo "reporter timed out becoming READY" >&2
    return 1
}

stop_reporter() {
    if [ -n "${REPORTER_PID:-}" ] && kill -0 "$REPORTER_PID" 2>/dev/null; then
        kill -TERM "$REPORTER_PID" 2>/dev/null || true
        wait "$REPORTER_PID" 2>/dev/null || true
    fi
}

# Let pending events make their way through the debouncer / poll.
drain() {
    sleep 1
}

# Substring-style assertion; pattern is matched as a fixed string.
assert_event() {
    pattern=$1
    desc=$2
    if grep -Fq "$pattern" "$EVENTS_LOG"; then
        echo "  PASS  $desc"
    else
        echo "  FAIL  $desc"
        echo "        looking for: $pattern"
        echo "        events were:"
        sed 's/^/          /' "$EVENTS_LOG"
        FAILED=$((FAILED + 1))
    fi
}

assert_no_event() {
    pattern=$1
    desc=$2
    if grep -Fq "$pattern" "$EVENTS_LOG"; then
        echo "  FAIL  $desc"
        echo "        unexpected line:"
        grep -F "$pattern" "$EVENTS_LOG" | sed 's/^/          /'
        FAILED=$((FAILED + 1))
    else
        echo "  PASS  $desc"
    fi
}
