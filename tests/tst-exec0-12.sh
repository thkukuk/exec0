#!/bin/sh

LANG=C.UTF-8

# Test 12: Error handling for an empty --preserve-env= list
echo -e "\nTest 12: Error handling for empty preserve-env"
if ./exec0 --preserve-env= 2>&1 | grep -q "requires a non-empty list" && ./exec0 --preserve-env= 2>&1 | grep -q "^exec0 \["; then
    echo "✓ PASS: Empty preserve-env list is properly handled"
else
    echo "✗ FAIL: Empty preserve-env list handling failed"
    exit 1
fi
