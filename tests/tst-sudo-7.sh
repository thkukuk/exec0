#!/bin/sh

LANG=C.UTF-8

# Test 7: Help and version
echo "Test 7: Help and version commands"
if ./sudo --help | grep -q "^usage: sudo " && ./sudo --version | grep -q "^sudo (exec0)"; then
    echo "✓ PASS: Help and version commands work"
else
    echo "✗ FAIL: Help and version commands failed"
    exit 1
fi
