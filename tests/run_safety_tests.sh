#!/bin/sh

binary=${1:-build/test/test_safety}
status=0

for test_name in \
    null-input \
    get-value-scalar \
    truncated-unicode \
    bounded-whitespace \
    get-value-null-key \
    get-value-null-output \
    parse-array-null-output \
    deep-nesting
do
    printf '%-32s' "$test_name"
    if "$binary" "$test_name"; then
        echo PASS
    else
        echo FAIL
        status=1
    fi
done

exit "$status"
