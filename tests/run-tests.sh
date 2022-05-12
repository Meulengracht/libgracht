#!/bin/bash

# get all test programs that start with gclient in
# the tests subfolder, they each end with a number, so
# make sure they are sorted
TESTS=$(find tests -name "*gclient*" -type f | sort -V)

# get all server programs so we can test both single-threaded
# and multi-threaded versions.
SERVERS=$(find tests -name "gserver*" -type f)

# iterate servers, and then for each server we want to start
# each test program
for SERVER in $SERVERS
do
    # start the server in the background
    echo "Running tests for $SERVER"
    $SERVER &

    echo "Waiting for server to start"
    sleep 2
    for TEST in $TESTS
    do
        echo "Running $TEST"
        $TEST
    done
done

wait
