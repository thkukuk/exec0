#!/bin/sh

LANG=C.UTF-8

# Test 11: Error handling for missing arguments
echo -e "\nTest 11: Error handling for missing arguments"
if ./exec0 -u 2>&1 | grep -q "option requires an argument" && ./exec0 -u 2>&1 | grep -q "^exec0 \["; then
    echo "✓ PASS: Missing arguments are properly handled"
else
    echo "✗ FAIL: Missing argument handling failed"
    exit 1
fi
