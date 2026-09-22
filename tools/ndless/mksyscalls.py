#!/usr/bin/env python3
"""Generate ndless-sdk/include/syscall-addrs.h without PHP.

A line-for-line port of vendor/Ndless/ndless/src/tools/MakeSyscalls/mkSyscalls.php, which is the
only thing in the Ndless build that needs a PHP interpreter. Porting it is cheaper than adding PHP
to the build requirements, and it is the ONLY generated input standing between this repo and a
resources build.

The ORDER of _IDC_FILES is load-bearing: resources/utils.c indexes syscall_addrs[] by OS number and
that number is this array's index. It is copied verbatim from the PHP and asserted against it by
tools/eval/gate_syscalls.py.
"""
import re
import sys
from pathlib import Path

# Verbatim from mkSyscalls.php. Order matters (see module docstring).
_IDC_FILES = [
    "OS_ncas-3.1.0.idc", "OS_cas-3.1.0.idc", "OS_ncascx-3.1.0.idc", "OS_cascx-3.1.0.idc",
    "OS_cmc-3.1.0.idc", "OS_cascmc-3.1.0.idc",
    "OS_ncas-3.6.0.idc", "OS_cas-3.6.0.idc", "OS_ncascx-3.6.0.idc", "OS_cascx-3.6.0.idc",
    "OS_ncas-3.9.0.idc", "OS_cas-3.9.0.idc", "OS_ncascx-3.9.0.idc", "OS_cascx-3.9.0.idc",
    "OS_ncas-3.9.1.idc", "OS_cas-3.9.1.idc", "OS_ncascx-3.9.1.idc", "OS_cascx-3.9.1.idc",
    "OS_ncascx-4.0.0.idc", "OS_cascx-4.0.0.idc",
    "OS_ncascx-4.0.3.idc", "OS_cascx-4.0.3.idc",
    "OS_ncascx-4.2.0.idc", "OS_cascx-4.2.0.idc",
    "OS_ncascx-4.3.0.idc", "OS_cascx-4.3.0.idc",
    "OS_ncascx-4.4.0.idc", "OS_cascx-4.4.0.idc",
    "OS_ncascx-4.5.0.idc", "OS_cascx-4.5.0.idc",
    "OS_ncascx-4.5.1.idc", "OS_cascx-4.5.1.idc",
    "OS_ncascx-4.5.3.idc", "OS_cascx-4.5.3.idc",
    "OS_ncascx2-5.2.0.771.idc", "OS_ncascx2t-5.2.0.771.idc", "OS_cascx2-5.2.0.771.idc",
    "OS_ncascx-4.5.4.48.idc", "OS_cascx-4.5.4.48.idc",
    "OS_ncascx2-5.3.0.564.idc", "OS_ncascx2t-5.3.0.564.idc", "OS_cascx2-5.3.0.564.idc",
    "OS_ncascx-4.5.5.79.idc", "OS_cascx-4.5.5.79.idc",
    "OS_ncascx2-6.2.0.333.idc", "OS_ncascx2t-6.2.0.333.idc", "OS_cascx2-6.2.0.333.idc",
    "OS_ncascx2-6.4.0.74.idc", "OS_ncascx2t-6.4.0.74.idc", "OS_cascx2-6.4.0.74.idc",
]

# Listed => absence is expected and prints no warning.
_UNIMPORTANT = {
    "OS_ncascx-3.9.0.idc", "OS_cascx-3.9.0.idc", "OS_ncas-3.9.1.idc", "OS_cas-3.9.1.idc",
}

# TI renamed these; our syscall keeps the one name everywhere.
_RENAMED = {
    "TI_NN_SS_StartService": "TI_NN_StartService",
    "TI_NN_SS_StopService": "TI_NN_StopService",
}

_DEFINE_RE = re.compile(r"#define e_(.+?) (\d+)( .*)?$")
_MAKENAME_RE = re.compile(r'\s*MakeName\s*\((.+),\s+"(.+)"\);')


def read_syscall_names(list_header: Path) -> list[str]:
    """Names in syscall-number order, from the START_OF_LIST/END_OF_LIST block."""
    names: list[str] = []
    inside = False
    for line in list_header.read_text(errors="replace").splitlines():
        if not inside:
            if "START_OF_LIST" in line:
                inside = True
            continue
        if "END_OF_LIST" in line:
            break
        m = _DEFINE_RE.search(line)
        if not m:
            continue
        number = int(m.group(2))
        # The PHP dies here rather than emitting a misaligned table, and so do we: a gap would
        # silently shift every later syscall's address by one.
        if number != len(names):
            sys.exit(f"Error: syscall numbers not contiguous (expected {len(names)}, got {number})")
        names.append(m.group(1))
    return names


def read_idc(path: Path) -> dict[str, str]:
    """symbol -> address literal, as written in the IDA dump."""
    out: dict[str, str] = {}
    for line in path.read_text(errors="replace").splitlines():
        m = _MAKENAME_RE.match(line)
        if not m or m.group(1) == "0xFFFFFFFF":
            continue
        out[_RENAMED.get(m.group(2), m.group(2))] = m.group(1)
    return out


def build(idc_dir: Path, out_path: Path, list_header: Path, quiet: bool = False) -> None:
    names = read_syscall_names(list_header)
    if not quiet:
        print(f"Found {len(names)} syscalls!")

    per_os: list[dict[str, str]] = []
    for idc_file in _IDC_FILES:
        path = idc_dir / idc_file
        if not path.is_file():
            per_os.append({})
            if not quiet and idc_file not in _UNIMPORTANT:
                print(f"Warning: couldn't open '{path}'!")
            continue
        per_os.append(read_idc(path))

    rows = []
    for idc_file, addrs in zip(_IDC_FILES, per_os):
        cells = []
        for name in names:
            addr = addrs.get(name)
            if addr is None:
                # Only warn when the file WAS opened -- an absent file already warned above, and
                # repeating it once per syscall buries every real warning.
                if not quiet and idc_file not in _UNIMPORTANT and addrs:
                    print(f"Warning: {idc_file} is missing syscall {name}!")
                addr = "0x0"
            cells.append(addr)
        rows.append(cells)

    body = "".join(
        "{\n" + "".join(f"{c},\n" for c in cells) + "},\n" for cells in rows
    )
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(
        "#ifndef SYSCALL_ADDR_LIST_H\n"
        "#define SYSCALL_ADDR_LIST_H\n"
        "\n"
        "//This file has been autogenerated by mkSyscalls.php\n"
        "\n"
        "#if defined(__cplusplus) && defined(STAGE1)\n"
        "constexpr\n"
        "#endif // __cplusplus && STAGE1\n"
        f"unsigned int syscall_addrs[{len(_IDC_FILES)}][{len(names)}] =\n"
        "{\n\n"
        + body
        + "};\n"
        "\n"
        "#endif // !SYSCALL_ADDR_LIST_H"
    )


def main() -> None:
    if len(sys.argv) != 3:
        sys.exit(f"Usage: {sys.argv[0]} /path/to/idc/files /path/to/output/file")
    idc_dir = Path(sys.argv[1])
    out_path = Path(sys.argv[2])
    build(idc_dir, out_path, idc_dir.parent / "../../../../ndless-sdk/include/syscall-list.h")


if __name__ == "__main__":
    main()
