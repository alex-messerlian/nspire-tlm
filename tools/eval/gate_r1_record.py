#!/usr/bin/env python3
"""PERMANENT GATE: an R1 document's RECORD must be the relation its CALL rearranges.

WHY THIS EXISTS, and it is not hypothetical. R1 harvests the record span from documents the
generator already built, so the span cannot drift from the device's. But `head` stays the store
formula while A51 rewrites ~24% of document TEXTS with scrambled symbols, so the first document per
head was often a scrambled one:

    <q>solve V=I*R for I</q><r>V=mu*sigma | V:V mu:A sigma:ohm | ...<tool>solve<arg>V=I*R<arg>I

The question asks about one relation, the record shows another, and the answer rearranges the first.
EVERY STRUCTURAL CHECK PASSES. It is well-formed, the record span is a real span, the tool call
executes, the result is correct, provenance is clean. The supervision is simply wrong. It was found
by hand-reading eight documents and by nothing else, which is the argument for the read-before-volume
rule and also the argument for this gate: a defect found by reading should not need reading twice.

THE PREDICATE IS THE PROPERTY, not a proxy for it: the relation printed in field 1 of the record
span, compared to the relation passed as the first argument of the call. No normalisation, because
these two strings are copied from the same source and any difference at all is the defect.
"""
import json, pathlib, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
CORPUS = ROOT / "corpus/synth_sample.jsonl"


def scan(lines):
    """Return (n, mismatches) over every document that calls a SYMBOLIC tool on its own record.

    THREE TOOLS, TWO SHAPES, and the difference is not cosmetic:
      solve(FORMULA, var)  -- the argument is the WHOLE relation, so it must equal the record
      diff(RHS, var)       -- the argument is the RIGHT-HAND SIDE, because you differentiate an
      integ(RHS, var)         expression and not an equation

    Checking diff against the whole formula would fail every C1 document, and checking solve against
    the RHS would fail every R1 one. A gate that applied one rule to both would be wrong in one
    direction and would look like a corpus defect."""
    n = 0
    bad = []
    for t in lines:
        if "<r>" not in t:
            continue
        for tool, whole in (("solve", True), ("diff", False), ("integ", False)):
            tag = f"<tool>{tool}<arg>"
            if tag not in t:
                continue
            n += 1
            rec = t.split("<r>", 1)[1].split(" | ", 1)[0]
            want = rec if whole else (rec.split("=", 1)[1] if "=" in rec else rec)
            got = t.split(tag, 1)[1].split("<arg>", 1)[0]
            if want != got:
                bad.append((f"{tool}: {want}", got, t[:110]))
            break
    return n, bad


def main():
    if not CORPUS.exists():
        print("CANNOT CHECK: corpus/synth_sample.jsonl absent. Not a pass.")
        return 2

    # A CLEAN TREE AND A DISABLED CHECK PRINT THE SAME PASS, so the predicate is exercised on a
    # known-bad and a known-good on every run. The known-bad is the ACTUAL defect, verbatim.
    BAD = ("<q>solve V=I*R for I</q><r>V=mu*sigma | V:V mu:A sigma:ohm | missing:mu | c | "
           "fit:high<tool>solve<arg>V=I*R<arg>I</tool><res>I=V/R</res><a>x<end>")
    GOOD = ("<q>solve V=I*R for I</q><r>V=I*R | V:V I:A R:ohm | missing:none | c | "
            "fit:high<tool>solve<arg>V=I*R<arg>I</tool><res>I=V/R</res><a>x<end>")
    # The calculus shape needs its own controls: a diff call carries the RHS, so a gate that
    # checked it against the whole formula would fire on every correct C1 document.
    CBAD = ("<q>d/dt</q><r>v=v_0+a*t | v:m/s | missing:none | c | fit:high"
            "<tool>diff<arg>x_0+v_0*t<arg>t</tool><res>v_0</res><a>x<end>")
    CGOOD = ("<q>d/dt</q><r>v=v_0+a*t | v:m/s | missing:none | c | fit:high"
             "<tool>diff<arg>v_0+a*t<arg>t</tool><res>a</res><a>x<end>")
    if not scan([CBAD])[1] or scan([CGOOD])[1]:
        print("  CONTROL BROKEN: the diff shape is not separated from a mismatched one")
        return 2
    if not scan([BAD])[1] or scan([GOOD])[1]:
        print("  CONTROL BROKEN: the predicate does not separate the known-bad from the known-good")
        return 2
    print("  controls: A51-scrambled fires and matching does not, for BOTH the solve "
          "(whole formula) and the diff (right-hand side) shapes")

    lines = [json.loads(l)["text"] for l in CORPUS.open()]
    n, bad = scan(lines)
    print(f"  {len(lines):,} documents, {n:,} carry a symbolic tool call")
    if n == 0:
        # Not a pass and not a failure: REARRANGE=0 is a legitimate configuration, and saying so is
        # different from saying the check ran clean.
        print("  NOTHING TO CHECK: no symbolic-tool documents in this corpus (REARRANGE=0?). Not a pass.")
        return 2
    for rec_f, call_f, t in bad[:4]:
        print(f"  MISMATCH  record {rec_f!r}  call {call_f!r}\n     {t}")
    if bad:
        print(f"\n  FAIL: {len(bad)} of {n} solve documents show a record that is not the relation "
              f"the call rearranges ({100*len(bad)/n:.1f}%).")
        return 1
    print(f"  PASS: all {n:,} documents call a symbolic tool on the relation their call rearranges")
    return 0


if __name__ == "__main__":
    sys.exit(main())
