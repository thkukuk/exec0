#!/bin/sh

LANG=C.UTF-8

# Test 9: su(exec0) propagates the exit status of the command run with -c, instead of always exiting 0
# itself (regression test: this used to always report 0, no matter how the backend command exited).
echo -e "\nTest 9: Exit status propagation (-c)"
./su -c 'exit 22'
rc1=$?
./su -c true
rc2=$?
./su -c false
rc3=$?
if [ "$rc1" -eq 22 ] && [ "$rc2" -eq 0 ] && [ "$rc3" -eq 1 ]; then
    echo "✓ PASS: Exit status propagation works"
else
    echo "✗ FAIL: Exit status propagation failed (got $rc1, $rc2, $rc3; expected 22, 0, 1)"
    exit 1
fi
