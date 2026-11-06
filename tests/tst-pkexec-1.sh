#!/bin/sh

LANG=C.UTF-8

# Test 1: Basic functionality
echo "Test 1: Basic privilege escalation"
if ./pkexec whoami | grep -q "root"; then
    echo "✓ PASS: Basic pkexec(exec0) functionality works"
else
    echo "✗ FAIL: Basic pkexec(exec0) functionality failed"
    exit 1
fi

