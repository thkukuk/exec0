#!/bin/sh

LANG=C.UTF-8

# Test 5: Specific environment variable preservation (--preserve-env=list)
echo "Test 5: Specific environment variable preservation"
TEST1="value1"
TEST2="value2"
export TEST1 TEST2
if ./sudo --preserve-env=TEST1,TEST2 printenv | grep -q "TEST1" && \
   ./sudo --preserve-env=TEST1,TEST2 printenv | grep -q "TEST2"; then
    echo "✓ PASS: Specific environment variable preservation works"
else
    echo "✗ FAIL: Specific environment variable preservation failed"
    exit 1
fi
