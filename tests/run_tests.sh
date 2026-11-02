#!/usr/bin/env bash
#
# run_tests.sh - build and run the MiniMail automated test suite.
#
# The suite starts its own server on a scratch port and data directory, so it
# never touches the real data/ folder.
set -u

cd "$(dirname "$0")/.." || exit 1

PORT="${PORT:-9191}"

echo "== Building =="
make -s all || exit 1

echo
echo "== Protocol / functional tests (port $PORT) =="
python3 tests/test_protocol.py --port "$PORT"
rc=$?

echo
if [ "$rc" -eq 0 ]; then
    echo "All automated tests passed."
else
    echo "Some tests FAILED (exit $rc)."
fi
exit "$rc"
