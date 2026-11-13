#!/bin/sh

LANG=C.UTF-8

# Test 17: a bare "sudo" (no command, and no -i/-s/-v/-k/-K either)
# is an error, not an implicit root shell

echo "Test 17: bare sudo is rejected, not an implicit root shell"
if ! ./sudo < /dev/null > /dev/null 2>&1; then
    echo "✓ PASS: bare sudo fails as expected"
else
    echo "✗ FAIL: bare sudo unexpectedly succeeded"
    exit 1
fi

echo ""
echo "Test 17: sudo -u USER alone (no command) is also rejected"
if ! ./sudo -u root < /dev/null > /dev/null 2>&1; then
    echo "✓ PASS: 'sudo -u root' without a command fails as expected"
else
    echo "✗ FAIL: 'sudo -u root' without a command unexpectedly succeeded"
    exit 1
fi

echo ""
echo "Test 17: sudo -i (no further command) still opens a shell"
if ./sudo -i pwd | tr -d '\r' | grep -q "^/root$"; then
    echo "✓ PASS: 'sudo -i' still works"
else
    echo "✗ FAIL: 'sudo -i' no longer works"
    exit 1
fi
