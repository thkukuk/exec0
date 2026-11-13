#!/bin/sh

LANG=C.UTF-8

# Test 10: Error handling for invalid options
echo -e "\nTest 10: Error handling for invalid options"
if ./exec0 -X 2>&1 | grep -q "invalid option" && ./exec0 -X 2>&1 | grep -q "^exec0 \["; then
    echo "✓ PASS: Invalid options are properly handled"
else
    echo "✗ FAIL: Invalid option handling failed"
    exit 1
fi
