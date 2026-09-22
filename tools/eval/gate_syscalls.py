#!/usr/bin/env python3
"""A134. The PHP-free syscall table must be the table Ndless actually ships.

tools/ndless/mksyscalls.py replaced upstream's mkSyscalls.php so the loader can be built without a
PHP interpreter. A port of an address table is exactly the kind of change that cannot be checked by
reading it: every cell is a bare hex literal, and one wrong cell is a syscall that jumps into the
middle of the operating system on one OS version and nowhere else.

The check is therefore against an EXTERNAL fact, not against the port's own output. The Ndless
project ships a prebuilt ndless_resources.tns whose .rodata contains this table verbatim. Row 49 --
OS_cascx2-6.4.0.74, the only OS this project runs on -- was extracted from that release binary and
its digest recorded below. If our generated row hashes to the same value, our addresses ARE their
addresses for the OS we ship on.

Measured when this gate was written: 48 of 50 rows are byte-identical to the release. The two that
differ are OS_ncascx-3.9.0 and OS_cascx-3.9.0, where OUR vendored tree carries IDA dumps the
release build did not have (290 and 292 nonzero cells against all zeros). That is more data, not
different data, and neither is an OS this project supports.
"""
import hashlib
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "ndless"))
from mksyscalls import _IDC_FILES, build  # noqa: E402

IDC = ROOT / "vendor" / "Ndless" / "ndless" / "src" / "tools" / "MakeSyscalls" / "idc"
LIST = ROOT / "vendor" / "Ndless" / "ndless-sdk" / "include" / "syscall-list.h"

# The OS this project ships on, and the digest of its row as extracted from the Ndless release
# binary ndless_resources.tns (r2022, sha256 5994e5096d16e2c4...).
DEVICE_IDC = "OS_cascx2-6.4.0.74.idc"
DEVICE_ROW_SHA256 = "2a68d099fbadb3982f5931dd5fbaa1e622664de24b8b5d87c149bccd32666ab1"

EXPECT_OS = 50
EXPECT_SYSCALLS = 343


def rows_from_header(text: str) -> list[list[int]]:
    body = text.split("{\n\n", 1)[1].rsplit("};", 1)[0]
    return [[int(x, 0) for x in blk.replace(",\n", " ").split()]
            for blk in re.findall(r"\{\n(.*?)\},\n", body, re.S)]


def generate() -> list[list[int]]:
    with tempfile.TemporaryDirectory() as td:
        out = Path(td) / "syscall-addrs.h"
        build(IDC, out, LIST, quiet=True)
        return rows_from_header(out.read_text())


def check(rows: list[list[int]]) -> list[str]:
    bad: list[str] = []
    if len(rows) != EXPECT_OS:
        bad.append(f"expected {EXPECT_OS} OS rows, got {len(rows)}")
    widths = {len(r) for r in rows}
    if widths != {EXPECT_SYSCALLS}:
        bad.append(f"expected every row to be {EXPECT_SYSCALLS} wide, got widths {sorted(widths)}")
    try:
        i = _IDC_FILES.index(DEVICE_IDC)
    except ValueError:
        return bad + [f"{DEVICE_IDC} is no longer in the OS order list"]
    if i >= len(rows):
        return bad + [f"row {i} missing"]
    digest = hashlib.sha256(b"".join(struct.pack("<I", v) for v in rows[i])).hexdigest()
    if digest != DEVICE_ROW_SHA256:
        bad.append(f"row {i} ({DEVICE_IDC}) is not the table Ndless ships:\n"
                   f"      want {DEVICE_ROW_SHA256}\n      have {digest}")
    return bad


def main() -> int:
    rows = generate()
    bad = check(rows)

    # Positive control. A gate over a generated table is worthless unless a wrong table fails it,
    # and "wrong" here means one cell -- the smallest defect the port could actually introduce.
    i = _IDC_FILES.index(DEVICE_IDC)
    mutated = [list(r) for r in rows]
    mutated[i][0] ^= 4
    if not check(mutated):
        bad.append("CONTROL SURVIVED: flipping one address in the device row did not fail the "
                   "check, so the check is not reading the row it claims to")

    print(f"gate_syscalls: {len(rows)} OS rows x {len(rows[0])} syscalls, "
          f"device row {i} ({DEVICE_IDC})")
    if bad:
        for b in bad:
            print(f"  FAIL: {b}")
        return 1
    print("  device row matches the Ndless release byte for byte; control fires")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
