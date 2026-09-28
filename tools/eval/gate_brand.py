#!/usr/bin/env python3
"""A134. No shipped artefact shows the student a string that says Ndless.

The loader is a rebranded fork (resources/brand.py, resources/sdk-branding.patch). Branding is the
kind of change that regresses silently: a rebuilt static archive, a re-cloned vendor tree, or a
`make` that reports "Nothing to be done" all put the upstream strings back with nothing on screen
to say so. That last one happened twice while this was being written.

WHAT THIS CHECKS, and what it does not. It reads the built binaries, not the sources, because the
sources are not what ships and the archives are not built from anything tracked. It searches both
UTF-8 and UTF-16LE, because the install banner is a u"" literal and a UTF-8-only search finds
nothing and reports clean.

THE EXEMPTIONS ARE STRINGS THAT EXIST AND ARE NOT VISIBLE. Each is declared with a reason and a
binary it is allowed in; anything else fails. An undeclared occurrence is a failure even if it is
lowercase, because "it is probably a path" is the reasoning that lets a real one through.
"""
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# Built artefacts a student's calculator receives.
SHIPPED = [
    ROOT / "build" / "chattlm_support.tns",
    ROOT / "build" / "chattlm.tns",
    ROOT / "build" / "ChatTLM_Setup.tns",
    ROOT / "installer" / "ndless_installer.bin",
]

# Strings that reach a dialog, a banner, or the file browser. Case-sensitive: the capital N is the
# branded form, and every legitimate remaining occurrence is lowercase.
FORBIDDEN = [
    "Ndless",
    "ndlessly.wordpress.com",
    "The application crashed",
    "bFLT loader",
    "ndless_resources",          # the filename the student would see
    "ndless/startup",            # the folder the student would see
    "documents/ndless",          # ditto
]

# (forbidden string, artefact basename) pairs that are allowed, each with its reason. An
# occurrence that is not on this list fails, whatever it looks like: "it is probably a path" is the
# reasoning that lets a real one through. An entry that matches nothing also fails, because an
# exemption nobody trips is indistinguishable from coverage.
EXEMPT = {
    ("documents/ndless", "chattlm.tns"):
        "a LEGACY data directory, probed by device_app.c and nspire_tlm.c AFTER the preferred "
        "/documents/chattlm/. Kept so a calculator holding files from an earlier install keeps "
        "working. A lookup path, never displayed.",
}


def hits(blob: bytes, needle: str) -> int:
    return blob.count(needle.encode()) + blob.count(needle.encode("utf-16-le"))


def scan(path: Path) -> tuple[list[str], set[tuple[str, str]]]:
    """(violations, exemptions actually used) for one artefact."""
    blob = path.read_bytes()
    bad, used = [], set()
    for s in FORBIDDEN:
        n = hits(blob, s)
        if not n:
            continue
        if (s, path.name) in EXEMPT:
            used.add((s, path.name))
            continue
        bad.append(f"{s!r} x{n}")
    return bad, used


def main() -> int:
    missing = [p for p in SHIPPED if not p.is_file()]
    if missing:
        # "cannot check" must never share an exit status with "checked and clean".
        for p in missing:
            print(f"  FAIL: {p.relative_to(ROOT)} not built -- nothing was checked for it")
        return 1

    bad = False
    used: set[tuple[str, str]] = set()
    for p in SHIPPED:
        found, u = scan(p)
        used |= u
        print(f"  {str(p.relative_to(ROOT)):36} {len(p.read_bytes()):>8} B  "
              f"{'clean' if not found else '; '.join(found)}")
        bad |= bool(found)

    unused = set(EXEMPT) - used
    for s_, f_ in sorted(unused):
        print(f"  FAIL: exemption {s_!r} in {f_} matched nothing -- it reads as coverage and is not")
        bad = True
    for s_, f_ in sorted(used):
        print(f"  exempt {s_!r} in {f_}: {EXEMPT[(s_, f_)].split('.')[0]}.")

    # CONTROL 1, on the real subject: put one branded string back into OUR binary and require the
    # gate to fail. This is the defect the gate exists for -- a rebuild that loses the branding.
    probe = ROOT / "build" / "chattlm_support.tns"
    blob = probe.read_bytes()
    marker = b"chattlm_load: can't open doc"
    assert marker in blob, "control anchor moved; re-point it at a string brand.py rewrites"
    mutated = ROOT / "build" / ".gate_brand_control.tns"
    mutated.write_bytes(blob.replace(marker, b"Ndless_load:  can't open doc", 1))
    try:
        found, _ = scan(mutated)
        if not found:
            print("  FAIL: CONTROL SURVIVED -- reintroducing 'Ndless' into our own loader did not "
                  "fail the gate")
            bad = True
        else:
            print(f"  control 1 (branding put back into our loader): fails as required -- {found[0]}")
    finally:
        mutated.unlink(missing_ok=True)

    # CONTROL 2: the unmodified Ndless loader is precisely what this gate exists to stop shipping.
    # NDLESS_RELEASE names an official release's ndless_resources.tns; by default, the one a full
    # build of vendor/Ndless produces from Ndless's own sources.
    ref = Path(os.environ.get("NDLESS_RELEASE", ROOT / "vendor/Ndless/ndless/calcbin/ndless_resources.tns"))
    if ref.is_file():
        found, _ = scan(ref)
        if not found:
            print("  FAIL: CONTROL SURVIVED -- the unmodified Ndless release passes this gate")
            bad = True
        else:
            print(f"  control 2 (unmodified Ndless release): fails as required -- {found[0]}")
    else:
        print(f"  note: control 2 skipped, {ref} absent (control 1 still ran)")

    print("gate_brand: FAIL" if bad else "gate_brand: PASS")
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
