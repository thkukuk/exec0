#!/bin/sh

LANG=C.UTF-8

# Test 16: plain "exec0 -i" with no command (as opposed to "exec0 -i COMMAND", already covered by
# test 6) must still start a real interactive login shell: dash-prefixed argv[0] and the target
# user's home directory as cwd. With no command line, exec0 leaves the shell invocation entirely to
# the broker's own default semantics, so this exercises that path specifically.
echo -e "\nTest 16: Plain \"exec0 -i\" with no command starts a login shell"
output=$(printf 'echo "$0"\npwd\nexit\n' | ./exec0 -u root -i 2>&1 | tr -d '\r')
if echo "$output" | grep -qE '^-.*bash$' && echo "$output" | grep -qx '/root'; then
    echo "✓ PASS: Plain \"exec0 -i\" starts a login shell in the target user's home directory"
else
    echo "✗ FAIL: Plain \"exec0 -i\" did not start a login shell (got: '$output')"
    exit 1
fi
