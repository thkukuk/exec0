#!/bin/sh

LANG=C.UTF-8

# Test 2: pkexec should change to new home directory
echo "Test 2: Change to home directory"
if ./exec0 --mode=pkexec pwd | tr -d '\r' | grep -q "^/root$"; then
    echo "✓ PASS: pkexec(exec0) changed to home directory"
else
    echo "✗ FAIL: pkexec(exec0) did not change to home directory"
    exit 1
fi

