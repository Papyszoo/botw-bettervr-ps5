#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
"${CXX:-c++}" -std=c++20 -Wall -Wextra -Werror -I"$root/port/cemu" "$root/tools/tests/test-texunit-memo.cpp" -o "$work/test-texunit-memo"
"$work/test-texunit-memo"
