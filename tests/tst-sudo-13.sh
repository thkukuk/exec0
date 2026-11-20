#!/bin/sh

LANG=C.UTF-8

# Test 13: sudo(exec0) propagates the exit status of COMMAND, including death by an "unclean" signal
# as 128+signal, the same convention $? uses for a directly run command. (SIGTERM/SIGHUP/SIGINT/SIGPIPE
# are deliberately excluded from this: systemd itself treats those as a clean shutdown for a service of
# this type, the same way run0 does.)
echo "Test 13: Exit status propagation"
./exec0 --mode=sudo true
rc1=$?
./exec0 --mode=sudo false
rc2=$?
./exec0 --mode=sudo sh -c 'exit 22'
rc3=$?
./exec0 --mode=sudo sh -c 'kill -KILL $$'
rc4=$?
./exec0 --mode=sudo sh -c 'kill -SEGV $$'
rc5=$?
if [ "$rc1" -eq 0 ] && [ "$rc2" -eq 1 ] && [ "$rc3" -eq 22 ] && [ "$rc4" -eq 137 ] && [ "$rc5" -eq 139 ]; then
    echo "✓ PASS: Exit status propagation works"
else
    echo "✗ FAIL: Exit status propagation failed (got $rc1, $rc2, $rc3, $rc4, $rc5; expected 0, 1, 22, 137, 139)"
    exit 1
fi
