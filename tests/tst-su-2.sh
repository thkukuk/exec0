#!/bin/sh

LANG=C.UTF-8

# Test 2: su stays in the current directory by default
echo "Test 2: Stay in the current directory"
if ./su -c pwd | tr -d '\r' | grep -q "^$(pwd)$"; then
    echo "✓ PASS: su(exec0) did not change the directory"
else
    echo "✗ FAIL: su(exec0) did change the directory"
    exit 1
fi
