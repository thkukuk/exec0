#!/bin/sh

LANG=C.UTF-8

# Test 6: User switching (-u). Uses -D /tmp to avoid tripping over the invoking user's working
# directory not being accessible to the target user, which is a directory-permission question, not
# something sudo(exec0) itself needs to care about.
echo "Test 6: User switching (-u)"
if ! getent passwd test0 >/dev/null 2>&1; then
    echo "✗ SKIP: user test0 not available"
    exit 77
fi
if ./exec0 --mode=sudo -D /tmp -u test0 whoami | tr -d '\r' | grep -q "^test0$"; then
    echo "✓ PASS: User switching works"
else
    echo "✗ FAIL: User switching failed"
    exit 1
fi
