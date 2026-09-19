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
    """Return (n_r1, mismatches). One pass, no regex: both fields are at fixed tag boundaries."""
    n = 0
    bad = []
    for t in lines:
        if "<tool>solve<arg>" not in t or "<r>" not in t:
            continue
        n += 1
        rec_f = t.split("<r>", 1)[1].split(" | ", 1)[0]
        call_f = t.split("<tool>solve<arg>", 1)[1].split("<arg>", 1)[0]
        if rec_f != call_f:
            bad.append((rec_f, call_f, t[:110]))
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
    if not scan([BAD])[1] or scan([GOOD])[1]:
        print("  CONTROL BROKEN: the predicate does not separate the known-bad from the known-good")
        return 2
    print("  controls: the A51-scrambled shape fires, the matching shape does not")

    lines = [json.loads(l)["text"] for l in CORPUS.open()]
    n, bad = scan(lines)
    print(f"  {len(lines):,} documents, {n:,} carry a solve call")
    if n == 0:
        # Not a pass and not a failure: REARRANGE=0 is a legitimate configuration, and saying so is
        # different from saying the check ran clean.
        print("  NOTHING TO CHECK: no solve documents in this corpus (REARRANGE=0?). Not a pass.")
        return 2
    for rec_f, call_f, t in bad[:4]:
        print(f"  MISMATCH  record {rec_f!r}  call {call_f!r}\n     {t}")
    if bad:
        print(f"\n  FAIL: {len(bad)} of {n} solve documents show a record that is not the relation "
              f"the call rearranges ({100*len(bad)/n:.1f}%).")
        return 1
    print(f"  PASS: all {n:,} solve documents show the relation their call rearranges")
    return 0


if __name__ == "__main__":
    sys.exit(main())
