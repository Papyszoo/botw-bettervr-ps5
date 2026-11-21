#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# The console's keyboard dialog's state (port/app/ime_dialog.cpp) on the PC, with the system's calls
# replaced by a fake (tools/tests/test-ime-dialog.cpp). Needs clang++ (or g++).
set -euo pipefail
root=$(cd "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
"${CXX:-c++}" -std=c++20 -Wall -Wextra -Werror -pthread -DPS5IME_CHECK -I"$root/port" \
    "$root/tools/tests/test-ime-dialog.cpp" "$root/port/app/ime_dialog.cpp" -o "$work/test-ime-dialog"
"$work/test-ime-dialog"
