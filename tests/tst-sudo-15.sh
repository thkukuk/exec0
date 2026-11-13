#!/bin/sh

LANG=C.UTF-8

# Test 15: -s (--shell) runs the invoking user's $SHELL as the target user, without the login-shell
# semantics (dash-prefixed argv[0], home directory) that -i applies.
echo "Test 15: -s runs \$SHELL as a non-login shell"
if SHELL=/bin/bash ./sudo -s printenv SHELL | tr -d '\r' | grep -q "^/bin/bash$" && \
   ./sudo -s pwd | tr -d '\r' | grep -q "^$(pwd)$"; then
    echo "✓ PASS: -s runs \$SHELL and keeps the working directory"
else
    echo "✗ FAIL: -s handling failed"
    exit 1
fi
