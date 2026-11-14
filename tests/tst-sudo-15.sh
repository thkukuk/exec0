#!/bin/sh

LANG=C.UTF-8

# Test 15: -s (--shell) runs the invoking user's $SHELL as the target user, without the login-shell
# semantics (dash-prefixed argv[0], home directory) that -i applies.
echo "Test 15: -s runs \$SHELL as a non-login shell"

# Pick a $SHELL value different from root's actual login shell, so the test can't pass by
# accident if sudo ignored $SHELL and fell back to root's passwd entry instead.
root_shell=$(getent passwd root | cut -d: -f7)
if [ "$root_shell" = "/bin/bash" ]; then
    test_shell=/bin/sh
else
    test_shell=/bin/bash
fi

if SHELL=$test_shell ./sudo -s printenv SHELL | tr -d '\r' | grep -q "^$test_shell$" && \
   ./sudo -s pwd | tr -d '\r' | grep -q "^$(pwd)$"; then
    echo "✓ PASS: -s runs \$SHELL and keeps the working directory"
else
    echo "✗ FAIL: -s handling failed"
    exit 1
fi
