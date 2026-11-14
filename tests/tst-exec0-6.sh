#!/bin/sh

LANG=C.UTF-8

# Test 6: a login shell (-i, --login) changes to the target user's home directory and actually
# sources /etc/profile before running the given command (PROFILEREAD is set there, and nowhere else,
# on this system) -- unlike -s, which must not do either. Also checks that an explicit command's
# arguments reach it completely unmangled, regardless of any shell metacharacters they contain, since
# the login-shell wrapper execs straight into the command rather than re-parsing it through an
# intermediate shell.
echo -e "\nTest 6: Login shell functionality (-i)"
if ./exec0 -u root -i printenv PROFILEREAD | tr -d '\r' | grep -qx "true" && \
   ./exec0 -u root -i pwd | tr -d '\r' | grep -q "^/root$" && \
   ./exec0 -u root -i -- bash -c 'echo "$1"' _ 'hello $HOME `whoami`' | tr -d '\r' | grep -qx 'hello $HOME `whoami`'; then
    echo "✓ PASS: Login shell sources /etc/profile, changes directory, and passes command arguments through verbatim"
else
    echo "✗ FAIL: Login shell functionality failed"
    exit 1
fi
