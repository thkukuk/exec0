#!/bin/sh

LANG=C.UTF-8

# Test 7: Help and version
echo "Test 7: Help and version commands"
if ./exec0 --mode=sudo --help | grep -q "^usage: sudo " && ./exec0 --mode=sudo --version | grep -q "^sudo (exec0)"; then
    echo "✓ PASS: Help and version commands work"
else
    echo "✗ FAIL: Help and version commands failed"
    exit 1
fi
