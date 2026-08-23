#!/usr/bin/env python3
"""Validate the load-critical LV2 PRX records and emulate its relocations."""

from __future__ import annotations

import struct
import sys
from dataclasses import dataclass
from pathlib import Path


PT_LOAD = 1
PT_SCE_PPURELA = 0x700000A4
EXPECTED_NIDS = (0xBC9A0086, 0xAB779874)


class ValidationError(RuntimeError):
    pass


@dataclass
class Segment:
    file_offset: int
    virtual_address: int
    physical_address: int
    file_size: int
    memory_size: int
    flags: int
    base: int
    data: bytearray


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValidationError(message)


def resolve(segments: list[Segment], address: int, size: int = 1) -> tuple[Segment, int]:
    for segment in segments:
        offset = address - segment.base
        if 0 <= offset and offset + size <= segment.memory_size:
            return segment, offset
    raise ValidationError(f"runtime pointer 0x{address:x} is outside all LOAD segments")


def load_prx(path: Path) -> tuple[bytes, list[Segment], tuple[int, ...]]:
    raw = path.read_bytes()
    require(len(raw) >= 64 and raw[:9] == b"\x7fELF\x02\x02\x01\x66\x00", "not a LV2 ELF64")
    header = struct.unpack_from(">16sHHIQQQIHHHHHH", raw, 0)
    require(header[1] == 0xFFA4, "ELF type is not ET_SCE_PPURELA (0xffa4)")
    require(header[2] == 21 and header[4] == 0, "unexpected machine or entry point")
    require(header[6] == 0 and header[12] == 0, "section headers must be stripped")
    phoff, phentsize, phnum = header[5], header[9], header[10]
    require(phentsize == 56 and phoff + phentsize * phnum <= len(raw), "invalid program headers")
    programs = [
        struct.unpack_from(">IIQQQQQQ", raw, phoff + index * phentsize)
        for index in range(phnum)
    ]
    load_headers = [program for program in programs if program[0] == PT_LOAD and program[6]]
    require(load_headers, "PRX has no LOAD segments")
    segments = []
    for index, program in enumerate(load_headers):
        _, flags, offset, vaddr, paddr, filesz, memsz, _alignment = program
        require(offset + filesz <= len(raw) and filesz <= memsz, "invalid LOAD segment")
        memory = bytearray(memsz)
        memory[:filesz] = raw[offset : offset + filesz]
        segments.append(
            Segment(offset, vaddr, paddr, filesz, memsz, flags,
                    0x10000000 + index * 0x01000000, memory)
        )
    reloc_headers = [program for program in programs if program[0] == PT_SCE_PPURELA]
    require(len(reloc_headers) == 1, "PRX must have exactly one SCE_PPURELA segment")
    relocation = reloc_headers[0]
    require(relocation[2] + relocation[5] <= len(raw) and relocation[5] % 24 == 0,
            "invalid SCE_PPURELA segment")
    return raw, segments, relocation


def apply_relocations(raw: bytes, segments: list[Segment], relocation: tuple[int, ...]) -> int:
    offset, size = relocation[2], relocation[5]
    count = size // 24
    for index in range(count):
        target_offset, unknown, value_index, address_index, kind, value_offset = struct.unpack_from(
            ">QHBBIQ", raw, offset + index * 24
        )
        require(unknown == 0, "unexpected relocation reserved field")
        require(address_index < len(segments), "relocation target segment is invalid")
        target_segment = segments[address_index]
        require(target_offset < target_segment.memory_size, "relocation target is out of bounds")
        value = value_offset if value_index == 0xFF else (
            segments[value_index].base + value_offset
            if value_index < len(segments) else -1
        )
        require(value >= 0, "relocation value segment is invalid")
        target_address = target_segment.base + target_offset
        if kind == 1:
            require(target_offset + 4 <= target_segment.memory_size, "ADDR32 target is truncated")
            struct.pack_into(">I", target_segment.data, target_offset, value & 0xFFFFFFFF)
        elif kind in (4, 5, 6, 57):
            require(target_offset + 2 <= target_segment.memory_size, "ADDR16 target is truncated")
            if kind == 4:
                result = value
            elif kind == 5:
                result = value >> 16
            elif kind == 6:
                result = (value >> 16) + (1 if value & 0x8000 else 0)
            else:
                result = value >> 2
            struct.pack_into(">H", target_segment.data, target_offset, result & 0xFFFF)
        elif kind in (38, 44):
            require(target_offset + 8 <= target_segment.memory_size, "ADDR64 target is truncated")
            result = value if kind == 38 else value - target_address
            struct.pack_into(">Q", target_segment.data, target_offset, result & 0xFFFFFFFFFFFFFFFF)
        else:
            raise ValidationError(f"unsupported output relocation type {kind}")
    return count


def validate_module(segments: list[Segment]) -> None:
    first = segments[0]
    require(first.physical_address >= first.file_offset, "module p_paddr precedes segment")
    module_offset = first.physical_address - first.file_offset
    require(module_offset + 52 <= first.memory_size, "module info is outside segment zero")
    values = struct.unpack_from(">H2s28sIIIII", first.data, module_offset)
    attributes, version, raw_name, toc, exports_start, exports_end, imports_start, imports_end = values
    name = raw_name[:27].split(b"\0", 1)[0].decode("ascii")
    require(attributes == 0 and version == b"\x00\x01", "unexpected module attributes/version")
    require(name == "dualsense_fix", f"unexpected module name {name!r}")
    require(toc == 0, "module TOC field should be zero for the exported start records")
    require(exports_start < exports_end and imports_start < imports_end,
            "module export/import bounds are invalid")

    export_segment, export_offset = resolve(segments, exports_start, 28)
    export = struct.unpack_from(">BBHHHHHBBBBIII", export_segment.data, export_offset)
    size, auxiliary, library_version, library_attributes = export[:4]
    function_count, variable_count, tls_count = export[4:7]
    library_name, nids, entries = export[-3:]
    require(size == 28 and auxiliary == 0 and library_version == 0,
            "special export record header is invalid")
    require(library_attributes == 0x8000 and function_count == 2,
            "module_start/module_stop export attributes are invalid")
    require(variable_count == 0 and tls_count == 0 and library_name == 0,
            "special export record contains unexpected entries")
    nid_segment, nid_offset = resolve(segments, nids, 8)
    entry_segment, entry_offset = resolve(segments, entries, 8)
    require(struct.unpack_from(">II", nid_segment.data, nid_offset) == EXPECTED_NIDS,
            "module_start/module_stop NIDs are invalid")
    start, stop = struct.unpack_from(">II", entry_segment.data, entry_offset)
    require(start != 0 and stop != 0, "module start/stop entry pointers are null")
    resolve(segments, start, 8)
    resolve(segments, stop, 8)
    resolve(segments, exports_end, 0)
    resolve(segments, imports_start, 1)
    resolve(segments, imports_end, 0)


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} module.prx", file=sys.stderr)
        return 2
    try:
        raw, segments, relocation = load_prx(Path(sys.argv[1]))
        count = apply_relocations(raw, segments, relocation)
        validate_module(segments)
    except (OSError, UnicodeError, struct.error, ValidationError) as error:
        print(f"validate_prx: {error}", file=sys.stderr)
        return 1
    print(f"validated loadable PRX structure: {len(segments)} LOAD segments, {count} relocations")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
