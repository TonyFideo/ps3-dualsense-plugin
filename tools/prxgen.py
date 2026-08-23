#!/usr/bin/env python3
"""Convert a PSL1GHT PPC64 ET_DYN into a relocatable LV2 PRX.

PSL1GHT emits ordinary ELF dynamic relocations.  LV2 PRX modules instead use
ET_SCE_PPURELA (0xffa4) and a PT_SCE_PPURELA segment containing compact
24-byte relocation records.  This converter deliberately supports only the
relocations produced by this project and fails closed if the link changes.
"""

from __future__ import annotations

import argparse
import struct
import sys
from dataclasses import dataclass
from pathlib import Path


PT_LOAD = 1
PT_SCE_PPURELA = 0x700000A4
SHT_SYMTAB = 2
SHT_RELA = 4
ET_DYN = 3
ET_SCE_PPURELA = 0xFFA4
EM_PPC64 = 21

R_PPC64_ADDR32 = 1
R_PPC64_ADDR16_LO = 4
R_PPC64_ADDR16_HI = 5
R_PPC64_ADDR16_HA = 6
R_PPC64_RELATIVE = 22
R_PPC64_ADDR64 = 38
R_PPC64_REL64 = 44
R_PPC64_ADDR16_LO_DS = 57

SUPPORTED_RELOCATIONS = {
    R_PPC64_ADDR32,
    R_PPC64_ADDR16_LO,
    R_PPC64_ADDR16_HI,
    R_PPC64_ADDR16_HA,
    R_PPC64_RELATIVE,
    R_PPC64_ADDR64,
    R_PPC64_REL64,
    R_PPC64_ADDR16_LO_DS,
}


class PrxError(RuntimeError):
    pass


@dataclass(frozen=True)
class ProgramHeader:
    p_type: int
    p_flags: int
    p_offset: int
    p_vaddr: int
    p_paddr: int
    p_filesz: int
    p_memsz: int
    p_align: int


@dataclass(frozen=True)
class SectionHeader:
    name_offset: int
    sh_type: int
    sh_flags: int
    sh_addr: int
    sh_offset: int
    sh_size: int
    sh_link: int
    sh_info: int
    sh_addralign: int
    sh_entsize: int
    name: str = ""


@dataclass(frozen=True)
class Symbol:
    name: str
    value: int
    size: int
    section_index: int


def checked_slice(data: bytes, offset: int, size: int, what: str) -> bytes:
    if offset < 0 or size < 0 or offset + size > len(data):
        raise PrxError(f"{what} is outside the ELF file")
    return data[offset : offset + size]


def c_string(table: bytes, offset: int) -> str:
    if offset < 0 or offset >= len(table):
        raise PrxError("string table offset is invalid")
    end = table.find(b"\0", offset)
    if end < 0:
        raise PrxError("unterminated string table entry")
    return table[offset:end].decode("ascii", errors="strict")


def align(value: int, alignment: int) -> int:
    return (value + alignment - 1) & -alignment


class Elf64Be:
    def __init__(self, data: bytes) -> None:
        self.data = data
        if len(data) < 64 or data[:7] != b"\x7fELF\x02\x02\x01":
            raise PrxError("input is not a big-endian ELF64 file")
        fields = struct.unpack_from(">16sHHIQQQIHHHHHH", data, 0)
        (
            _ident,
            self.e_type,
            self.e_machine,
            self.e_version,
            self.e_entry,
            self.e_phoff,
            self.e_shoff,
            self.e_flags,
            self.e_ehsize,
            self.e_phentsize,
            self.e_phnum,
            self.e_shentsize,
            self.e_shnum,
            self.e_shstrndx,
        ) = fields
        if self.e_type != ET_DYN or self.e_machine != EM_PPC64:
            raise PrxError("input must be a PPC64 ET_DYN")
        if self.e_phentsize != 56 or self.e_shentsize != 64:
            raise PrxError("unexpected ELF header table size")
        self.programs = self._read_programs()
        self.sections = self._read_sections()

    def _read_programs(self) -> list[ProgramHeader]:
        programs = []
        checked_slice(
            self.data, self.e_phoff, self.e_phnum * self.e_phentsize,
            "program header table",
        )
        for index in range(self.e_phnum):
            values = struct.unpack_from(
                ">IIQQQQQQ", self.data, self.e_phoff + index * 56
            )
            programs.append(ProgramHeader(*values))
        return programs

    def _read_sections(self) -> list[SectionHeader]:
        checked_slice(
            self.data, self.e_shoff, self.e_shnum * self.e_shentsize,
            "section header table",
        )
        raw = []
        for index in range(self.e_shnum):
            values = struct.unpack_from(
                ">IIQQQQIIQQ", self.data, self.e_shoff + index * 64
            )
            raw.append(SectionHeader(*values))
        if self.e_shstrndx >= len(raw):
            raise PrxError("invalid section name string table")
        names_header = raw[self.e_shstrndx]
        names = checked_slice(
            self.data, names_header.sh_offset, names_header.sh_size,
            "section name string table",
        )
        return [
            SectionHeader(
                section.name_offset,
                section.sh_type,
                section.sh_flags,
                section.sh_addr,
                section.sh_offset,
                section.sh_size,
                section.sh_link,
                section.sh_info,
                section.sh_addralign,
                section.sh_entsize,
                c_string(names, section.name_offset),
            )
            for section in raw
        ]

    def symbols(self, section_index: int) -> list[Symbol]:
        section = self.sections[section_index]
        if section.sh_type not in (SHT_SYMTAB, 11) or section.sh_entsize != 24:
            raise PrxError("relocation section does not reference an ELF64 symbol table")
        if section.sh_link >= len(self.sections):
            raise PrxError("symbol string table index is invalid")
        strings_header = self.sections[section.sh_link]
        strings = checked_slice(
            self.data, strings_header.sh_offset, strings_header.sh_size,
            "symbol string table",
        )
        count = section.sh_size // section.sh_entsize
        result = []
        for index in range(count):
            name_offset, _info, _other, shndx, value, size = struct.unpack_from(
                ">IBBHQQ", self.data, section.sh_offset + index * 24
            )
            result.append(Symbol(c_string(strings, name_offset), value, size, shndx))
        return result

    def find_symbol(self, name: str) -> Symbol:
        for index, section in enumerate(self.sections):
            if section.sh_type != SHT_SYMTAB:
                continue
            for symbol in self.symbols(index):
                if symbol.name == name:
                    return symbol
        raise PrxError(f"required symbol {name!r} was not retained")


def segment_for_address(
    segments: list[ProgramHeader], address: int, *, allow_end: bool = False
) -> tuple[int, ProgramHeader]:
    for index, segment in enumerate(segments):
        upper = segment.p_vaddr + segment.p_memsz
        if segment.p_vaddr <= address < upper or (
            allow_end and segment.p_memsz and address == upper
        ):
            return index, segment
    raise PrxError(f"address 0x{address:x} is not inside a LOAD segment")


def segment_for_value(
    segments: list[ProgramHeader], address: int
) -> tuple[int, ProgramHeader]:
    try:
        return segment_for_address(segments, address, allow_end=True)
    except PrxError:
        # ELFv1 uses a TOC base around .got + 0x8000.  That base can legally be
        # beyond p_memsz; it still belongs to the preceding LOAD segment and
        # must be expressed as that segment's base plus an offset.
        candidates = [
            (index, segment)
            for index, segment in enumerate(segments)
            if segment.p_vaddr <= address
        ]
        if not candidates:
            raise
        index, segment = max(candidates, key=lambda item: item[1].p_vaddr)
        if address - segment.p_vaddr > 0xFFFFFFFF:
            raise PrxError(f"value 0x{address:x} is too far from a LOAD segment")
        return index, segment


def build_relocations(elf: Elf64Be, segments: list[ProgramHeader]) -> bytes:
    output = bytearray()
    counts: dict[int, int] = {}
    for section in elf.sections:
        if section.sh_type != SHT_RELA or section.sh_size == 0:
            continue
        if section.sh_entsize != 24 or section.sh_link >= len(elf.sections):
            raise PrxError(f"invalid relocation section {section.name}")
        symbols = elf.symbols(section.sh_link)
        count = section.sh_size // section.sh_entsize
        for index in range(count):
            r_offset, r_info, r_addend = struct.unpack_from(
                ">QQq", elf.data, section.sh_offset + index * 24
            )
            symbol_index = r_info >> 32
            relocation_type = r_info & 0xFFFFFFFF
            if relocation_type not in SUPPORTED_RELOCATIONS:
                raise PrxError(
                    f"unsupported relocation type {relocation_type} in {section.name}; "
                    "link with -Bsymbolic and update the converter deliberately"
                )
            address_index, address_segment = segment_for_address(segments, r_offset)
            if relocation_type == R_PPC64_RELATIVE:
                value = r_addend
                output_type = R_PPC64_ADDR64
            else:
                if symbol_index >= len(symbols):
                    raise PrxError("relocation symbol index is invalid")
                value = symbols[symbol_index].value + r_addend
                output_type = relocation_type

            if value == 0:
                value_index = 0xFF
                value_offset = 0
            else:
                value_index, value_segment = segment_for_value(segments, value)
                value_offset = value - value_segment.p_vaddr
            target_offset = r_offset - address_segment.p_vaddr
            output += struct.pack(
                ">QHBBIQ",
                target_offset,
                0,
                value_index,
                address_index,
                output_type,
                value_offset,
            )
            counts[output_type] = counts.get(output_type, 0) + 1
    if not output:
        raise PrxError("no relocations were converted")
    summary = ", ".join(f"type {kind}: {counts[kind]}" for kind in sorted(counts))
    print(f"converted {len(output) // 24} relocations ({summary})")
    return bytes(output)


def convert(input_path: Path, output_path: Path, module_symbol: str) -> None:
    elf = Elf64Be(input_path.read_bytes())
    segments = [
        program
        for program in elf.programs
        if program.p_type == PT_LOAD and program.p_memsz != 0
    ]
    if not segments or len(segments) > 0xFF:
        raise PrxError("invalid number of LOAD segments")
    module_info = elf.find_symbol(module_symbol)
    module_segment_index, module_segment = segment_for_address(
        segments, module_info.value
    )
    if module_segment_index != 0:
        raise PrxError("module info must be retained in the first LOAD segment")
    if module_info.size < 52:
        raise PrxError("module info record is unexpectedly small")

    relocations = build_relocations(elf, segments)
    program_count = len(segments) + 1
    cursor = align(64 + program_count * 56, 16)
    output_programs: list[ProgramHeader] = []
    payloads: list[bytes] = []
    module_file_offset = 0
    for index, segment in enumerate(segments):
        payload = checked_slice(
            elf.data, segment.p_offset, segment.p_filesz,
            f"LOAD segment {index}",
        )
        if index == 0:
            module_file_offset = cursor + module_info.value - segment.p_vaddr
        output_programs.append(
            ProgramHeader(
                PT_LOAD,
                segment.p_flags,
                cursor,
                segment.p_vaddr,
                module_file_offset if index == 0 else 0,
                len(payload),
                segment.p_memsz,
                16,
            )
        )
        payloads.append(payload)
        cursor = align(cursor + len(payload), 16)
    output_programs.append(
        ProgramHeader(
            PT_SCE_PPURELA, 0, cursor, 0, 0, len(relocations), 0, 16
        )
    )

    ident = bytearray(16)
    ident[:9] = b"\x7fELF\x02\x02\x01\x66\x00"
    header = struct.pack(
        ">16sHHIQQQIHHHHHH",
        bytes(ident),
        ET_SCE_PPURELA,
        EM_PPC64,
        1,
        0,
        64,
        0,
        0x01000000,
        64,
        56,
        program_count,
        0,
        0,
        0,
    )
    result = bytearray(header)
    for program in output_programs:
        result += struct.pack(">IIQQQQQQ", *program.__dict__.values())
    if len(result) > output_programs[0].p_offset:
        raise PrxError("program headers overlap the first segment")
    result += bytes(output_programs[0].p_offset - len(result))
    for index, payload in enumerate(payloads):
        expected = output_programs[index].p_offset
        if len(result) != expected:
            raise PrxError("internal segment layout error")
        result += payload
        if index + 1 < len(output_programs):
            result += bytes(output_programs[index + 1].p_offset - len(result))
    if len(result) != output_programs[-1].p_offset:
        raise PrxError("internal relocation layout error")
    result += relocations
    output_path.write_bytes(result)
    print(
        f"wrote {output_path} with {len(segments)} LOAD segments; "
        f"module info file offset 0x{module_file_offset:x}"
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--module-symbol", default="g_module_info")
    args = parser.parse_args()
    try:
        convert(args.input, args.output, args.module_symbol)
    except (OSError, UnicodeError, struct.error, PrxError) as error:
        print(f"prxgen: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
