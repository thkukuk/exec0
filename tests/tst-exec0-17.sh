#!/bin/sh

LANG=C.UTF-8

# Test 17: --background validates its argument and, with a tty attached, tints the terminal with it,
# re-asserting the color after an SGR sequence that resets it back to default.
echo -e "\nTest 17: --background sets and re-asserts the terminal background color"

if ./exec0 --background 'not-a-color' true 2>/dev/null; then
    echo "✗ FAIL: --background accepted an invalid ANSI color code"
    exit 1
fi

if ! ./exec0 --background '44' -u root true; then
    echo "✗ FAIL: --background rejected a valid ANSI color code"
    exit 1
fi

output=$(python3 -c "
import pty, os, sys, time
pid, fd = pty.fork()
if pid == 0:
    os.execvp('./exec0', ['./exec0', '--background', '44', '-u', 'root', 'printf', 'A\\033[0mB'])
else:
    time.sleep(0.5)
    try:
        data = os.read(fd, 65536)
    except OSError:
        data = b''
    sys.stdout.buffer.write(data)
    os.waitpid(pid, 0)
")

if echo "$output" | grep -qF $'\033[44m' && echo "$output" | grep -qF $'\033[0;44m'; then
    echo "✓ PASS: --background tints the terminal and re-asserts the color after a reset"
else
    echo "✗ FAIL: --background did not tint/re-assert the terminal background (got: '$output')"
    exit 1
fi
