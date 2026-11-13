#!/bin/sh

LANG=C.UTF-8

# Test 1: Basic functionality
echo "Test 1: Basic privilege escalation"
if ./sudo whoami | grep -q "root"; then
    echo "✓ PASS: Basic sudo(exec0) functionality works"
else
    echo "✗ FAIL: Basic sudo(exec0) functionality failed"
    exit 1
fi
