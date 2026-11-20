#!/bin/sh

LANG=C.UTF-8

# Test 4: pkexec(exec0) propagates the exit status of PROGRAM, instead of always exiting 0 itself
# (regression test: this used to always report 0, no matter how the backend command exited).
echo -e "\nTest 4: Exit status propagation"
./exec0 --mode=pkexec true
rc1=$?
./exec0 --mode=pkexec false
rc2=$?
./exec0 --mode=pkexec sh -c 'exit 22'
rc3=$?
if [ "$rc1" -eq 0 ] && [ "$rc2" -eq 1 ] && [ "$rc3" -eq 22 ]; then
    echo "✓ PASS: Exit status propagation works"
else
    echo "✗ FAIL: Exit status propagation failed (got $rc1, $rc2, $rc3; expected 0, 1, 22)"
    exit 1
fi
