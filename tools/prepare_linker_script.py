#!/usr/bin/env python3
"""Add the PRX module-info output section to PSL1GHT's pinned lv2.ld."""

from pathlib import Path
import sys


NEEDLE = "\t.rodata.sceResident : { KEEP (*(.rodata.sceResident)) }\n"
INSERT = NEEDLE + (
    "\t.rodata.sceModuleInfo : { "
    "KEEP (*(.rodata.sceModuleInfo .rodata.sceModuleInfo.*)) }\n"
)


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} input-lv2.ld output-lv2.ld", file=sys.stderr)
        return 2
    source = Path(sys.argv[1]).read_text(encoding="utf-8")
    if source.count(NEEDLE) != 1:
        print("prepare_linker_script: PSL1GHT lv2.ld layout changed", file=sys.stderr)
        return 1
    Path(sys.argv[2]).write_text(source.replace(NEEDLE, INSERT), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
