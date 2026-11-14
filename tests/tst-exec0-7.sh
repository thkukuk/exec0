#!/bin/sh

LANG=C.UTF-8

# Test 7: Environment variable preservation (-E)
echo -e "\nTest 7: Environment variable preservation (-E)"
TEST_VAR="test123"
export TEST_VAR
if ./exec0 -u root -E printenv TEST_VAR | grep -q "test123"; then
    echo "✓ PASS: Environment variable preservation works"
else
    echo "✗ FAIL: Environment variable preservation failed"
    exit 1
fi
