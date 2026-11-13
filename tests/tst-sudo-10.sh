#!/bin/sh

LANG=C.UTF-8

# Test 10: Error handling for an empty --preserve-env= list
echo "Test 10: Error handling for empty preserve-env"
if ./sudo --preserve-env= 2>&1 | grep -q "^usage:"; then
    echo "✓ PASS: Empty preserve-env list is properly handled"
else
    echo "✗ FAIL: Empty preserve-env list handling failed"
    exit 1
fi
