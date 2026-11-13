#!/bin/sh

LANG=C.UTF-8

# Test 2: sudo stays in the current directory by default
echo "Test 2: Stay in the current directory"
if ./sudo pwd | tr -d '\r' | grep -q "^$(pwd)$"; then
    echo "✓ PASS: sudo(exec0) did not change the directory"
else
    echo "✗ FAIL: sudo(exec0) did change the directory"
    exit 1
fi
