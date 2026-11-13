#!/bin/sh

LANG=C.UTF-8

# Test 3: a login shell (-i, --login) changes to the target user's home directory and makes the
# shell a login shell (dash-prefixed argv[0])
echo "Test 3: Login shell functionality (-i)"
if ./sudo -i bash -c 'echo "$0"' | tr -d '\r' | grep -q "^-bash$" && \
   ./sudo -i pwd | tr -d '\r' | grep -q "^/root$"; then
    echo "✓ PASS: Login shell sets argv[0] correctly and changes directory"
else
    echo "✗ FAIL: Login shell functionality failed"
    exit 1
fi
