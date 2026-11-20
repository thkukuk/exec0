#!/bin/sh

LANG=C.UTF-8

# Test 11: -k, -K and -v are accepted for compatibility: they don't actually manage a timestamp
# cache (the broker authenticates via polkit on every invocation), but their effect on whether a
# command may/must follow should still match sudo(1).

echo "Test 11: -k exits successfully without a command"
if ./exec0 --mode=sudo -k; then
    echo "✓ PASS: -k exits with 0"
else
    echo "✗ FAIL: -k did not exit with 0"
    exit 1
fi

echo ""
echo "Test 11: --reset-timestamp exits successfully without a command"
if ./exec0 --mode=sudo --reset-timestamp; then
    echo "✓ PASS: --reset-timestamp exits with 0"
else
    echo "✗ FAIL: --reset-timestamp did not exit with 0"
    exit 1
fi

echo ""
echo "Test 11: -K exits successfully without a command"
if ./exec0 --mode=sudo -K; then
    echo "✓ PASS: -K exits with 0"
else
    echo "✗ FAIL: -K did not exit with 0"
    exit 1
fi

echo ""
echo "Test 11: --remove-timestamp exits successfully without a command"
if ./exec0 --mode=sudo --remove-timestamp; then
    echo "✓ PASS: --remove-timestamp exits with 0"
else
    echo "✗ FAIL: --remove-timestamp did not exit with 0"
    exit 1
fi

echo ""
echo "Test 11: -v exits successfully without a command"
if ./exec0 --mode=sudo -v; then
    echo "✓ PASS: -v exits with 0"
else
    echo "✗ FAIL: -v did not exit with 0"
    exit 1
fi

echo ""
echo "Test 11: -K with a command is rejected"
if ./exec0 --mode=sudo -K whoami 2>&1 | grep -q "cannot be used with a command"; then
    echo "✓ PASS: -K with command is properly rejected"
else
    echo "✗ FAIL: -K with command was not rejected"
    exit 1
fi

echo ""
echo "Test 11: -v with a command is rejected"
if ./exec0 --mode=sudo -v whoami 2>&1 | grep -q "cannot be used with a command"; then
    echo "✓ PASS: -v with command is properly rejected"
else
    echo "✗ FAIL: -v with command was not rejected"
    exit 1
fi

echo ""
echo "Test 11: -k with a command runs the command"
if ./exec0 --mode=sudo -k whoami | grep -q "root"; then
    echo "✓ PASS: -k with command runs the command"
else
    echo "✗ FAIL: -k with command did not run the command"
    exit 1
fi
