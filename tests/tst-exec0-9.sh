#!/bin/sh

LANG=C.UTF-8

# Test 9: User switching (-u). Uses -D /tmp to avoid tripping over the invoking user's working
# directory not being accessible to the target user, which is a directory-permission question, not
# something exec0 itself needs to care about.
echo -e "\nTest 9: User switching (-u)"
if ./exec0 -D /tmp -u test0 -- whoami | tr -d '\r' | grep -q "^test0$"; then
    echo "✓ PASS: User switching works"
else
    echo "✗ FAIL: User switching failed"
    exit 1
fi
