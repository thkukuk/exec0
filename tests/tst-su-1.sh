#!/bin/sh

LANG=C.UTF-8

# Test 1: Basic functionality
echo "Test 1: Basic privilege escalation"
if ./exec0 --mode=su -c whoami | grep -q "root"; then
    echo "✓ PASS: Basic su(exec0) functionality works"
else
    echo "✗ FAIL: Basic su(exec0) functionality failed"
    exit 1
fi
