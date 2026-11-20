#!/bin/sh

LANG=C.UTF-8

# Test 8: Error handling for invalid options
echo -e "\nTest 8: Error handling for invalid options"
if ./exec0 --mode=su -X 2>&1 | grep -i -q "su: unrecognized option" && ./exec0 --mode=su -X 2>&1 | grep -i -q "for more information"; then
    echo "✓ PASS: Invalid options are properly handled"
else
    echo "✗ FAIL: Invalid option handling failed"
    exit 1
fi
