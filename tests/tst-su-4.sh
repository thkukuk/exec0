#!/bin/sh

LANG=C.UTF-8

# Test 4: Environment variable preservation
echo -e "\nTest 4: Environment variable preservation (-p)"
TEST_VAR="test123"
export TEST_VAR
if ./exec0 --mode=su -p -c "printenv TEST_VAR" | grep -q "test123"; then
    echo "✓ PASS: Environment variable preservation works"
else
    echo "✗ FAIL: Environment variable preservation failed"
    exit 1
fi
