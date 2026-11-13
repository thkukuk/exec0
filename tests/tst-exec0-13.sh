#!/bin/sh

LANG=C.UTF-8

# Test 13: VAR=value environment variable assignments before the command

echo -e "\nTest 13: Single VAR=value assignment before command"
output=$(./exec0 -u root -- AAA=aaa printenv AAA 2>/dev/null | tr -d '\r')
if [ "$output" = "aaa" ]; then
    echo "✓ PASS: Single VAR=value assignment works"
else
    echo "✗ FAIL: Single VAR=value assignment failed (got: '$output')"
    exit 1
fi

echo ""
echo "Test 13: Multiple VAR=value assignments before command"
output=$(./exec0 -u root -- AAA=aaa BBB=bbb bash -c 'echo "$AAA $BBB"' 2>/dev/null | tr -d '\r')
if [ "$output" = "aaa bbb" ]; then
    echo "✓ PASS: Multiple VAR=value assignments work"
else
    echo "✗ FAIL: Multiple VAR=value assignments failed (got: '$output')"
    exit 1
fi

echo ""
echo "Test 13: VAR=value combined with -u option"
output=$(./exec0 -u root -- AAA=hello printenv AAA 2>/dev/null | tr -d '\r')
if [ "$output" = "hello" ]; then
    echo "✓ PASS: VAR=value combined with -u option works"
else
    echo "✗ FAIL: VAR=value combined with -u option failed (got: '$output')"
    exit 1
fi
