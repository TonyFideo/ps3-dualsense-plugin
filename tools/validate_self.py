#!/usr/bin/env python3
"""Check that the outer SELF identifies a VSH module and embeds a real PRX."""

import struct
import sys
from pathlib import Path


def fail(message: str) -> None:
    raise ValueError(message)


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} module.sprx", file=sys.stderr)
        return 2
    try:
        data = Path(sys.argv[1]).read_bytes()
        if len(data) < 0xB0 or data[:4] != b"SCE\0":
            fail("not an SCE SELF")
        version, flags, header_type = struct.unpack_from(">IHH", data, 4)
        if version != 2 or header_type != 1:
            fail("unexpected SCE version/type")
        if flags != 7:
            fail("SELF was not created with PSL1GHT's SPRX mode")
        extended_type, app_info_offset, elf_offset = struct.unpack_from(">QQQ", data, 0x20)
        if extended_type != 3:
            fail("SELF extended header type is invalid")
        if app_info_offset + 32 > len(data) or elf_offset + 64 > len(data):
            fail("SELF inner offsets are invalid")
        auth_id, vendor_id, app_type = struct.unpack_from(">QII", data, app_info_offset)
        if auth_id != 0x1070000052000001:
            fail("SELF auth ID is not sys/internal + vsh/module")
        if vendor_id != 0x01000002 or app_type != 4:
            fail("SELF vendor/application type is invalid")
        if data[elf_offset : elf_offset + 9] != b"\x7fELF\x02\x02\x01\x66\x00":
            fail("embedded ELF is not PPC64 LV2")
        if struct.unpack_from(">H", data, elf_offset + 16)[0] != 0xFFA4:
            fail("embedded ELF is not ET_SCE_PPURELA")
    except (OSError, struct.error, ValueError) as error:
        print(f"validate_self: {error}", file=sys.stderr)
        return 1
    print("validated VSH SELF wrapper: auth 0x1070000052000001, embedded PRX 0xffa4")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
