#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
test_output=$(mktemp -d)
trap 'rm -rf "$test_output"' EXIT
flags=(-std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined)
for test in recovery_policy bootloader_info; do
    g++ "${flags[@]}" -Itransitional/src "transitional/tests/${test}_test.cpp" -o "$test_output/$test"
    "$test_output/$test"
done
g++ "${flags[@]}" -Itransitional/tests/stubs transitional/src/boot_selection.cpp \
    transitional/tests/boot_selection_test.cpp transitional/tests/boot_selection_sdk_stub.cpp \
    -Wl,--wrap=esp_ota_set_boot_partition -o "$test_output/boot_selection"
"$test_output/boot_selection"
python3 -m unittest discover -s transitional/tests -p 'test_*.py' -v
