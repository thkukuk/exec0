#!/bin/sh

LANG=C.UTF-8

# Test 3: Help and version
echo -e "\nTest 3: Help and version commands"
if ./exec0 --help | grep -i -q -F 'exec0 [OPTIONS...] [-- COMMAND...]' && ./exec0 --version | grep -q "^exec0 (exec0)"; then
    echo "✓ PASS: Help and version commands work"
else
    echo "✗ FAIL: Help and version commands failed"
    exit 1
fi
