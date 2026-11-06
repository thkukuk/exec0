#!/bin/sh

LANG=C.UTF-8

# Test 2: exec0 stays in the current directory by default, and -D/--chdir overrides that
echo -e "\nTest 2: Working directory handling"
if ./exec0 -u root -- pwd | tr -d '\r' | grep -q "^$(pwd)$" && \
   ./exec0 -u root -D /tmp -- pwd | tr -d '\r' | grep -q "^/tmp$"; then
    echo "✓ PASS: Working directory handling works"
else
    echo "✗ FAIL: Working directory handling failed"
    exit 1
fi
