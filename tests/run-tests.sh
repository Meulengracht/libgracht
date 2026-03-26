#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEST_TIMEOUT=15
PASSED=0
FAILED=0
FAILURES=""

# get all test programs that start with gclient in
# the tests subfolder, they each end with a number, so
# make sure they are sorted
TESTS=$(find tests -regextype posix-extended -regex '.*/(gclient.*?exe|gclient[^.]*)' | sort -V)

# get all server programs so we can test both single-threaded
# and multi-threaded versions.
SERVERS=$(find tests -regextype posix-extended -regex '.*/(gserver.*?exe|gserver[^.]*)')

cleanup_server() {
    if [[ -n "${SERVER_PID:-}" ]] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill "$SERVER_PID" 2>/dev/null
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    SERVER_PID=""
}

trap cleanup_server EXIT

# iterate servers, and then for each server we want to start
# each test program
for SERVER in $SERVERS
do
    cleanup_server

    echo "========================================"
    echo "Running tests for $SERVER"
    echo "========================================"
    $SERVER &
    SERVER_PID=$!

    echo "Waiting for server to start (pid $SERVER_PID)"
    sleep 2

    if ! kill -0 "$SERVER_PID" 2>/dev/null; then
        echo "FATAL: Server $SERVER failed to start"
        FAILED=$((FAILED + 1))
        FAILURES="${FAILURES}  ${SERVER} (failed to start)\n"
        continue
    fi

    for TEST in $TESTS
    do
        TEST_NAME="$(basename "$TEST") [$(basename "$SERVER")]"
        printf "  %-40s " "$TEST_NAME"
        if timeout "$TEST_TIMEOUT" "$TEST" > /dev/null 2>&1; then
            echo "PASS"
            PASSED=$((PASSED + 1))
        else
            EXIT_CODE=$?
            if [[ $EXIT_CODE -eq 124 ]]; then
                echo "FAIL (timeout after ${TEST_TIMEOUT}s)"
            else
                echo "FAIL (exit $EXIT_CODE)"
            fi
            FAILED=$((FAILED + 1))
            FAILURES="${FAILURES}  ${TEST_NAME}\n"
        fi
    done
done

cleanup_server

echo ""
echo "========================================"
echo "Results: $PASSED passed, $FAILED failed"
echo "========================================"
if [[ $FAILED -gt 0 ]]; then
    echo "Failures:"
    echo -e "$FAILURES"
    exit 1
fi
exit 0
