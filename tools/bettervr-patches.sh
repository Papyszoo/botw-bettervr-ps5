#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# BotW-BetterVR on PS5: the changes to BetterVR's own files, kept as patches/bettervr/*.patch against the pinned
# commit.
#
#   tools/bettervr-patches.sh apply    put them on a branch named ps5 in .deps/BotW-BetterVR, and the ones
#                                     added since on it (ps5cemu_patches_apply in tools/env.sh)
#   tools/bettervr-patches.sh export   write the ps5 branch's commits back to patches/bettervr
#
# To change BotW-BetterVR: edit .deps/BotW-BetterVR on the ps5 branch, commit, then export.

set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/env.sh"
bettervr=$PS5CEMU_ROOT/.deps/BotW-BetterVR
patches=$PS5CEMU_ROOT/patches/bettervr
pin=$(python3 -c 'import json,sys; print(next(i["commit"] for i in json.load(open(sys.argv[1]))["items"] if i["name"] == "bettervr"))' \
    "$PS5CEMU_ROOT/tools/deps.json")

case ${1:-} in
apply)
    ps5cemu_patches_apply bettervr "$bettervr" "$patches" "$pin"
    ;;
export)
    [[ -z $(git -C "$bettervr" status --porcelain --untracked-files=no) ]] ||
        { echo "commit the changes in .deps/BotW-BetterVR first" >&2; exit 1; }
    rm -f "$patches"/*.patch
    mkdir -p "$patches"
    git -C "$bettervr" format-patch -q --no-signature --zero-commit --no-numbered -o "$patches" "$pin..ps5"
    ls "$patches"
    ;;
*)
    echo "usage: tools/bettervr-patches.sh apply|export" >&2
    exit 2
    ;;
esac
