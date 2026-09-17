#!/usr/bin/env python3
"""Generate a revision-pinned title selector mod from a user-supplied Red ROM.

No ROM data is distributed. Output must be a new directory.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct

ROM_SHA256 = "5ca7ba01642a3b27b0cc0b5349b52792795b62d3ed977e98a09390659af96b7b"


def generate(rom_path, module, output):
    with rom_path.open("rb") as source:
        rom = source.read(0x100001)
    if len(rom) != 0x100000:
        raise ValueError("expected a 1 MiB Pokemon Red ROM")
    if hashlib.sha256(rom).hexdigest() != ROM_SHA256:
        raise ValueError("unsupported ROM revision (expected English Pokemon Red reference image)")
    # Verified against pokered's native section layout: Home ends at $3fa6.
    cave = 0x3FA6
    select_trap, read_trap, select_stub = cave, cave + 1, cave + 2
    word = lambda n: struct.pack("<H", n)
    call = lambda n: b"\xcd" + word(n)
    code = bytearray(b"\xc9\xc9\x60\x69" + call(select_trap) + b"\x21\0\0" + call(read_trap) + b"\x7d\xc9")
    header_stub = cave + len(code)
    code += b"\xf5\xc5\xd5\xe5\x21\0\0" + call(read_trap)
    code += b"\xfa\xb5\xd0\xbd\x20"
    fallback_jump = len(code)
    code += b"\0\x11\xb8\xd0\x06\x1c\x0e\x01"
    loop = len(code)
    code += b"\xc5\xd5\x26\0\x69" + call(read_trap)
    code += b"\x7d\xd1\xc1\x12\x13\x0c\x05\x20"
    code += bytes([(loop - len(code) - 1) & 255])
    code += b"\xe1\xd1\xc1\xf1\xc9"
    code[fallback_jump] = len(code) - fallback_jump - 1
    code += b"\xe1\xd1\xc1\xf1\xc3\x37\x15"
    if cave + len(code) > 0x4000 or any(rom[cave:cave + len(code)]):
        raise ValueError("ROM0 code cave is not unused padding")
    patches = []
    def patch(bank, address, expected, replacement):
        offset = bank * 0x4000 + address % 0x4000
        if rom[offset:offset + len(expected)] != expected or len(expected) != len(replacement):
            raise ValueError(f"patch mismatch at {bank:02x}:{address:04x}")
        patches.append(dict(bank=bank, address=address, expected=expected.hex(), replacement=replacement.hex()))
    patch(0, cave, bytes(len(code)), code)
    patch(1, 0x44A3, bytes.fromhex("218845097e"), call(select_stub) + b"\0\0")
    patch(1, 0x452D, bytes.fromhex("cd3715"), call(header_stub))
    records = bytearray(257 * 29)
    for internal in range(1, 191):
        dex = rom[0x10 * 0x4000 + 0x1024 + internal - 1]
        if not dex:
            continue
        start = 0x425B if internal == 0x15 else 0x0E * 0x4000 + 0x3DE + (dex - 1) * 28
        record = bytearray([internal]) + rom[start:start + 28]
        record[1] = internal  # GetMonHeader overwrites dex number with internal ID.
        records[internal * 29:(internal + 1) * 29] = record
    records[256 * 29:257 * 29] = records[0x99 * 29:0x9A * 29]
    records[256 * 29 + 2] = 123  # Distinguishable external Bulbasaur-derived header.
    selection = list(rom[0x4588:0x4598])
    selection[0] = 256
    assets = {"selection": struct.pack("<16H", *selection), "records": records, "current": b"\xff\xff"}
    if not module.is_file():
        raise ValueError("native module is missing")
    output.mkdir(parents=True, exist_ok=False)
    for name, data in assets.items():
        (output / (name + ".bin")).write_bytes(data)
    shutil.copyfile(module, output / module.name)
    (output / "hooks.sym").write_text(f"00:{select_trap:04x} NativeSpeciesSelect\n00:{read_trap:04x} NativeSpeciesRead\n")
    manifest = dict(schemaVersion=1, id="pokered.title-species", version="1", target="gameboy",
                    romSha256=ROM_SHA256, nativeModule=module.name, symbols="hooks.sym",
                    trampolines=[dict(symbol="NativeSpeciesSelect", hookId=1),
                                 dict(symbol="NativeSpeciesRead", hookId=2)], patches=patches,
                    regions=[dict(name=n, size=len(d), file=n + ".bin") for n, d in assets.items()])
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--module", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        generate(args.rom, args.module, args.output)
    except (OSError, ValueError) as error:
        parser.error(str(error))
