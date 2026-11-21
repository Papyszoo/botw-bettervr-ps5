#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Emit the C source of an SDK stub library's exports, plus the names the SDK lacks.
#
#   tools/gen-scepad-stub.py SDK/libScePad.so NAME...
#
# The PS5 converter (ps5-native-tool link) learns which module an import belongs to from the stub
# that exports its symbol, and the NID is the hash of that symbol's name. libScePad's SDK stub has
# the Sense trigger setters but not the two vibration setters, so tools/link.sh links this stub in
# its place: the SDK's own export list, plus the names given here. Nothing from the SDK is copied
# beyond its public symbol names.

import struct
import sys


def symbols(path):
    data = open(path, "rb").read()
    if data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
        sys.exit(f"{path}: not a little-endian ELF64 file")
    shoff, = struct.unpack_from("<Q", data, 0x28)
    shentsize, shnum, _ = struct.unpack_from("<HHH", data, 0x3A)
    sections = []
    for index in range(shnum):
        name, kind, _flags, _addr, offset, size, link, _info, _align, _entsize = struct.unpack_from(
            "<IIQQQQIIQQ", data, shoff + index * shentsize)
        sections.append({"kind": kind, "offset": offset, "size": size, "link": link})
    dynamic = next((s for s in sections if s["kind"] == 11), None)  # SHT_DYNSYM
    if dynamic is None or dynamic["link"] >= len(sections):
        sys.exit(f"{path}: no dynamic symbol table")
    strings = sections[dynamic["link"]]
    table = data[dynamic["offset"]:dynamic["offset"] + dynamic["size"]]
    names = data[strings["offset"]:strings["offset"] + strings["size"]]
    found = []
    for index in range(len(table) // 24):
        name, info, _other, section, _value, _size = struct.unpack_from("<IBBHQQ", table, index * 24)
        binding, kind = info >> 4, info & 0xF
        if section == 0 or binding not in (1, 2) or kind not in (1, 2) or name == 0:
            continue
        end = names.index(b"\0", name)
        found.append((names[name:end].decode(), kind))
    return found


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__ or "usage: gen-scepad-stub.py SDK/libScePad.so NAME...")
    found = symbols(sys.argv[1])
    known = {name for name, _kind in found}
    for name in sys.argv[2:]:
        if name not in known:
            found.append((name, 2))  # STT_FUNC
            known.add(name)
    for name, kind in sorted(found):
        type_name = "@object" if kind == 1 else "@function"
        print(f'asm(".global {name}\\n"\n'
              f'    ".type {name} {type_name}\\n"\n'
              f'    "{name}:\\n");')


if __name__ == "__main__":
    main()
