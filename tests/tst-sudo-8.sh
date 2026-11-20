#!/bin/sh

LANG=C.UTF-8

# Test 8: Error handling for invalid options
echo "Test 8: Error handling for invalid options"
if ./exec0 --mode=sudo -X 2>&1 | grep -q "invalid option" && ./exec0 --mode=sudo -X 2>&1 | grep -q "^usage:"; then
    echo "✓ PASS: Invalid options are properly handled"
else
    echo "✗ FAIL: Invalid option handling failed"
    exit 1
fi
