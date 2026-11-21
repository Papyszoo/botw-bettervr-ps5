#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# The names this port resolves by NID must hash to the NIDs its code and research quote. The PS5
# packaging tool turns a stub's symbol name into the import's NID with SHA-1(name + suffix), first
# eight bytes reversed, base64's first eleven characters ('/' as '-'). A renamed function would
# silently import a different NID, so check the pairs here.

import base64
import hashlib
import sys

SUFFIX = bytes.fromhex("518d64a635ded8c1e6b039b1c3e55230")

# third_party/hmd2/hmd2.cpp: the DualSense import, the four Sense imports and the probe's six names.
PAIRS = {
    "scePadSetTriggerEffect": "2JgFB2n9oUM",
    "scePadVrControllerSetVibrationMode": "Wf6-PNCyY20",
    "scePadVrControllerSetVibration": "DGCwN1Lmmys",
    "scePadVrControllerSetTriggerEffect": "6Cdc9bbjrRY",
    "scePadVrControllerSetTriggerEffects": "v8P+9PRqg10",
    "sceHmd2SetVibration": "Al4qjNREVQQ",
    "sceHmd2GazeGetResult": "lAoFUedcfqA",
    "sceHmd2InternalGazeStart": "r93FLEZ3BBk",
    "sceHmd2InternalGazeStop": "IdYURdEdNFo",
}


def nid(name):
    digest = hashlib.sha1(name.encode() + SUFFIX).digest()
    return base64.b64encode(digest[:8][::-1]).decode()[:11].replace("/", "-")


failures = 0
for name, want in PAIRS.items():
    got = nid(name)
    print(f"{'PASS' if got == want else 'FAIL'} {name} {got} (expected {want})")
    failures += got != want
sys.exit(1 if failures else 0)
