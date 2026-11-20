#!/bin/sh

LANG=C.UTF-8

# Test 9: Error handling for missing arguments
echo "Test 9: Error handling for missing arguments"
if ./exec0 --mode=sudo -u 2>&1 | grep -q "option requires an argument" && ./exec0 --mode=sudo -u 2>&1 | grep -q "^usage:"; then
    echo "✓ PASS: Missing arguments are properly handled"
else
    echo "✗ FAIL: Missing argument handling failed"
    exit 1
fi
