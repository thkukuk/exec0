#!/bin/sh

LANG=C.UTF-8

# Test 7: Help and version
echo -e "\nTest 7: Help and version commands"
if ./su --help | grep -i -q -F 'su [options] [-] [<user> [<argument>...]]' && ./su --version | grep -q "^su (exec0)"; then
    echo "✓ PASS: Help and version commands work"
else
    echo "✗ FAIL: Help and version commands failed"
    exit 1
fi
