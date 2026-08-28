#!/usr/bin/env python3
"""PERMANENT GATE: every PF_BEG() is closed, and every declared profile slot is written.

WHY. `stage=ffn matmul` printed 0 ticks on the device -- impossible, the FFN is the largest matmul
in a transformer layer -- and 59% of every token landed in `unattributed`. Two causes, both
invisible to the compiler and to every existing check:

  1. PF_FFN was DECLARED in the enum and never appeared in a PF_END(). A slot nobody writes reads
     as zero, and zero is indistinguishable from "this stage is free".
  2. The softmax line ends with a bare PF_BEG() closed by the NEXT head's PF_END(PF_ATTN), so the
     LAST head's work -- and everything after it to the end of the layer -- was never attributed.

Neither is a syntax error. The instrumented build compiled, ran, and produced a plausible-looking
breakdown whose stages summed to 40% of the wall clock. The `unattributed` line was printed on
every run and read as loop overhead.

WHAT IT CHECKS, and the second is the one that matters: BEG/END balance per function, AND that
every slot in the enum is the target of at least one PF_END. A gate that only balanced the parens
would have passed the shipped code, because the parens WERE balanced -- across loop iterations.
"""
import pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC  = ROOT / "src/runq_nspire.c"


def main():
    if not SRC.exists():
        print(f"  CANNOT CHECK: {SRC} is missing. Refusing to report a pass.")
        return 2
    text = SRC.read_text()
    body = "\n".join(l for l in text.splitlines()
                     if not l.lstrip().startswith(("*", "/*", "//", "#define")))

    m = re.search(r"enum\s*\{([^}]*PF_N[^}]*)\}", body, re.S)
    if not m:
        print("  CANNOT CHECK: the PF_* enum was not found. Refusing to report a pass.")
        return 2
    slots = [s.strip() for s in m.group(1).split(",")]
    slots = [s for s in slots if s.startswith("PF_") and s != "PF_N"]

    ended = set(re.findall(r"PF_END\((PF_[A-Z_]+)\)", body))
    never = [s for s in slots if s not in ended]

    begs, ends = len(re.findall(r"PF_BEG\(\)", body)), len(re.findall(r"PF_END\(", body))

    print(f"  profile slots declared: {len(slots)}   written by a PF_END: {len(ended & set(slots))}")
    print(f"  PF_BEG() {begs}   PF_END() {ends}")
    bad = False
    if never:
        print(f"  FAIL: declared but NEVER written, so they read as 0 ticks: {', '.join(never)}")
        bad = True
    if begs != ends:
        print(f"  FAIL: {begs} PF_BEG() against {ends} PF_END() -- a probe is left open, and the")
        print("        work between it and the next PF_BEG() is silently discarded.")
        bad = True
    if bad:
        return 1
    print("  PASS: every slot is written and every PF_BEG() is closed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
