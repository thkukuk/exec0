#!/bin/sh

LANG=C.UTF-8

# Test 14: -H sets HOME to the target user's home directory without otherwise behaving like a login
# shell (the working directory itself is left alone, unlike -i).
echo "Test 14: -H sets HOME without changing the working directory"
if ./sudo -H printenv HOME | tr -d '\r' | grep -q "^/root$" && \
   ./sudo -H pwd | tr -d '\r' | grep -q "^$(pwd)$"; then
    echo "✓ PASS: -H sets HOME and keeps the working directory"
else
    echo "✗ FAIL: -H handling failed"
    exit 1
fi
