#!/bin/sh

LANG=C.UTF-8

# Test 3: pkexec should not change the directory
echo "Test 3: Stay in the current directory"
if ./pkexec --keep-cwd pwd | tr -d '\r' | grep -q "^$(pwd)$"; then
    echo "✓ PASS: pkexec(exec0) did not change the directory"
else
    echo "✗ FAIL: pkexec(exec0) did change the directory"
    exit 1
fi

