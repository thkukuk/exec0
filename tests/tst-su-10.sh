#!/bin/sh

LANG=C.UTF-8

# Test 10: su(exec0) reports death by an "unclean" signal as 128+signal, the same convention $? uses for a
# directly run command. (SIGTERM/SIGHUP/SIGINT/SIGPIPE are deliberately excluded from this: systemd itself
# treats those as a clean shutdown for a service of this type, the same way run0 does.)
echo -e "\nTest 10: Exit status propagation (killed by signal)"
./su -c 'kill -KILL $$'
rc1=$?
./su -c 'kill -SEGV $$'
rc2=$?
if [ "$rc1" -eq 137 ] && [ "$rc2" -eq 139 ]; then
    echo "✓ PASS: Signal exit status propagation works"
else
    echo "✗ FAIL: Signal exit status propagation failed (got $rc1, $rc2; expected 137, 139)"
    exit 1
fi
