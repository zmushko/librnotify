#!/bin/sh
# Run every stress test in this directory and report aggregate
# pass/fail. Exits 0 only if every test exited 0.
#
# Assumes the reporter binary has been built; the Makefile's `check`
# target builds it first.

set -u

cd -- "$(dirname -- "$0")"

passed=0
failed=0
failed_names=""

for t in deep_mkdir.sh atomic_save.sh symlink_no_follow.sh recursive_move.sh; do
    if [ ! -x "$t" ]; then
        echo "skip $t (not executable)"
        continue
    fi
    if ./"$t"; then
        passed=$((passed + 1))
    else
        failed=$((failed + 1))
        failed_names="$failed_names $t"
    fi
    echo
done

echo "---"
echo "summary: $passed passed, $failed failed"
if [ $failed -gt 0 ]; then
    echo "failed:$failed_names"
    exit 1
fi
