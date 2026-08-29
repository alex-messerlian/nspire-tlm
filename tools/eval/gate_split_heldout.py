#!/usr/bin/env python3
"""PERMANENT GATE: SELECT's separation claim must be true, or stated as false.

corpus/build_splits.py's docstring says:

    SEPARATION, on three axes, all asserted at build time:
      formulas  15 held-out formulas each, disjoint             (never trained on)

The first line is right that separation must be structural. The parenthesis is FALSE and the build
never asserted it: its assertions check SELECT-vs-REPORT disjointness, question overlap, stem-slice
overlap and DEV overlap -- none of which is "the model never trained on this formula".

MEASURED: the generator emits 164 formulas, units_holdout.json holds 30, and 21 overlap. Of SELECT's
13 formulas, 10 appear VERBATIM in the training corpus. SELECT is therefore mostly an
IN-DISTRIBUTION test presented as a generalisation test, and any number read off it inherits that
confusion -- including the retracted 16%.

This is not a propagation failure like the three before it. It is a claim held by a docstring while
the code asserted a different, weaker property -- the class this repo already records for `base()`
and for the 97.8% refusal rate. The fix is not to silence the gate: it is to carve the holdout OUT
of the store before generation, so "never trained on" becomes true by construction.
"""
import importlib.util, io, contextlib, json, pathlib, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
# Flipped 2026-08-28, when tools/carve_holdout.py made the claim true by construction: the holdout
# is drawn FROM the store and corpus/generate.py skips those formulas, so overlap is 0 by design
# rather than by a file name. From here this gate ENFORCES separation.
CLAIMS_HELD_OUT = True


def main():
    spec = importlib.util.spec_from_file_location("gen", ROOT / "corpus/generate.py")
    m = importlib.util.module_from_spec(spec)
    with contextlib.redirect_stdout(io.StringIO()):
        spec.loader.exec_module(m)
    trained = {r["f"] for r in m.recs}
    sel = {i["record"].split("|")[0].strip()
           for i in json.loads((ROOT / "corpus/split_select.json").read_text())}
    rep = {i["record"].split("|")[0].strip()
           for i in json.loads((ROOT / "corpus/split_report.json").read_text())}
    overlap_s, overlap_r = sel & trained, rep & trained
    print(f"  generator emits {len(trained)} formulas")
    print(f"  SELECT {len(sel)} formulas, {len(overlap_s)} trained on = {100*len(overlap_s)/max(1,len(sel)):.0f}%")
    print(f"  REPORT {len(rep)} formulas, {len(overlap_r)} trained on = {100*len(overlap_r)/max(1,len(rep)):.0f}%")
    if CLAIMS_HELD_OUT:
        if overlap_s or overlap_r:
            print("\n  FAIL: the splits claim held-out formulas and share them with training.")
            return 1
        print("\n  PASS: no split formula appears in the training set")
        return 0
    # The claim is currently FALSE and is recorded as false. The gate's job is to stop it being
    # quietly re-asserted: if the overlap ever reaches zero, flip CLAIMS_HELD_OUT and this becomes
    # a real separation gate.
    if not overlap_s and not overlap_r:
        print("\n  FAIL: the overlap is now zero -- set CLAIMS_HELD_OUT = True so this gate starts")
        print("  ENFORCING separation instead of merely recording its absence.")
        return 1
    print("\n  PASS (recording a KNOWN-FALSE claim, not a clean result):")
    print("  SELECT is mostly an IN-DISTRIBUTION test. Any number read off it must say so.")
    print("  To make it a generalisation test, carve the holdout OUT of the store before")
    print("  generation so 'never trained on' is true by construction.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
