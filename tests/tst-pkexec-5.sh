#!/bin/sh

LANG=C.UTF-8

# Test 5: pkexec(exec0) reports death by an "unclean" signal as 128+signal, the same convention $? uses for
# a directly run command.
echo -e "\nTest 5: Exit status propagation (killed by signal)"
./pkexec sh -c 'kill -KILL $$'
rc1=$?
./pkexec sh -c 'kill -SEGV $$'
rc2=$?
if [ "$rc1" -eq 137 ] && [ "$rc2" -eq 139 ]; then
    echo "✓ PASS: Signal exit status propagation works"
else
    echo "✗ FAIL: Signal exit status propagation failed (got $rc1, $rc2; expected 137, 139)"
    exit 1
fi
