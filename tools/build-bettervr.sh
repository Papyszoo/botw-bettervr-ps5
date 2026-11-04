#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# BotW-BetterVR on PS5 (M3): BetterVR with patches/bettervr, its ImGui and ImPlot, and the OpenXR
# runtime over hmd2 (port/bettervr/xr), built with the PS5 toolchain into build/bettervr/bettervr.o,
# one relocatable object whose symbols are local but port/bettervr/layer/exports.txt's
# (port/bettervr/layer/CMakeLists.txt). Cemu's link takes it (patches/cemu).
#
#   tools/build-bettervr.sh               apply patches/bettervr if needed, configure once, build
#   tools/build-bettervr.sh --reconfigure start the build folder over

set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/env.sh"
build=$PS5CEMU_BUILD/bettervr

if [[ ${1:-} == --reconfigure ]]; then
    rm -rf "$build"
    shift
fi
toolchain=$(sha1sum <"$PS5CEMU_TOOLCHAIN" | cut -c1-40)
if [[ -d $build && $(cat "$build/ps5-toolchain" 2>/dev/null) != "$toolchain" ]]; then
    echo "tools/ps5.cmake changed: $build starts over"
    rm -rf "$build"
fi

bash "$PS5CEMU_ROOT/tools/bettervr-patches.sh" apply

options=(-DCMAKE_TOOLCHAIN_FILE="$PS5CEMU_TOOLCHAIN" -DCMAKE_BUILD_TYPE=Release -DPS5CEMU_ROOT="$PS5CEMU_ROOT"
    -DPS5CEMU_SYSROOT="$PS5CEMU_SYSROOT")
if [[ ! -f $build/build.ninja || $(cat "$build/ps5-options" 2>/dev/null) != "${options[*]}" ]]; then
    mkdir -p "$build"
    cmake -S "$PS5CEMU_ROOT/port/bettervr/layer" -B "$build" -G Ninja -Wno-dev "${options[@]}" \
        >"$build.configure.log" 2>&1 || { tail -40 "$build.configure.log"; exit 1; }
    echo "${options[*]}" >"$build/ps5-options"
    echo "$toolchain" >"$build/ps5-toolchain"
fi
ninja -C "$build" -j "$JOBS" "$@"
