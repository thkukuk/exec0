#!/bin/sh

LANG=C.UTF-8

# Test 14: -s (--shell) runs the invoking user's $SHELL as the target user, without the login-shell
# semantics (dash-prefixed argv[0], home directory) that -i applies.
echo -e "\nTest 14: -s runs \$SHELL as a non-login shell"
if SHELL=/bin/bash ./exec0 -u root -s printenv SHELL | tr -d '\r' | grep -q "^/bin/bash$" && \
   ./exec0 -u root -s pwd | tr -d '\r' | grep -q "^$(pwd)$"; then
    echo "✓ PASS: -s runs \$SHELL and keeps the working directory"
else
    echo "✗ FAIL: -s handling failed"
    exit 1
fi
