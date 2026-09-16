#!/usr/bin/env python3
"""A D1 refusal must name a variable the question does NOT supply.

THE INVARIANT IS SOURCE-SIDE AND NEEDS NO MODEL: if the answer says "X is not given", then X must be
absent from the question's givens. It is the A12 rule made checkable -- a refusal that fires on a
satisfied precondition teaches the model to refuse a question it can answer.

IT FAILED 869 / 869 AND EVERY GATE PASSED. The A43e shuffle rebuilt the givens string from `vals`,
which still held the withheld variable because the withhold branch had filtered a local list instead
of the source. So 100% of D1 documents supplied the value their own answer declared missing:

    Q: "Where r = 2.42, k_e = 8988000000, q = 3.71e-07, calculate electric potential of a point charge."
    A: "I cannot answer that -- q is not given."

Nothing in the suite could see it. gate_refusal_cue searches n-grams; gate_spare_given measures
structural separability; neither compares the ANSWER's claim against the QUESTION's content. This
gate does, and the defect it was written for is its own negative control.
"""
import json, pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
# The separator accepts a colon OR an em dash. The corpus is migrating off the em dash at the
# user's request and a corpus generated before that change must still be checkable; the property
# here is "the refusal names a variable", and which punctuation precedes it is incidental. An
# answer that names NOTHING still counts as unparsed and still fails, which is the real guard.
CLAIM = re.compile(r"[:—-]\s*([A-Za-z_][A-Za-z0-9_]*) is not given")
GIVEN = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)\s*=")


def offenders(path):
    bad, n, unparsed = [], 0, 0
    for line in path.open():
        o = json.loads(line)
        if o.get("kind") != "D1":
            continue
        n += 1
        m = CLAIM.search(o.get("ans", ""))
        if not m:
            unparsed += 1          # CANNOT CHECK is not CLEAN -- counted and reported
            continue
        q = o["text"].split("<q>")[1].split("</q>")[0]
        if m.group(1) in set(GIVEN.findall(q)):
            bad.append((m.group(1), q[:88]))
    return bad, n, unparsed


def control():
    """The predicate must separate a known-bad from a known-good; a clean tree proves neither."""
    bad_q = "Where r = 2.42, q = 3.71e-07, calculate electric potential."
    good_q = "Where r = 2.42, calculate electric potential."
    ans = "I cannot answer that: q is not given."
    v = CLAIM.search(ans).group(1)
    return v in set(GIVEN.findall(bad_q)) and v not in set(GIVEN.findall(good_q))


def main():
    if not control():
        print("FAIL: gate_d1_withheld's predicate does not separate its controls"); return 1
    print("  control: a supplied-withheld question is flagged, a clean one is not")
    p = ROOT / "corpus/synth_sample.jsonl"
    if not p.exists():
        print("CANNOT CHECK: corpus absent -- not a pass"); return 2
    bad, n, unparsed = offenders(p)
    print(f"  D1 documents {n:,}   answers whose claim could not be parsed: {unparsed}")
    if unparsed:
        print("  FAIL: an unparsable claim is UNCHECKED, which is not the same as clean."); return 1
    print(f"  supplying the variable their own answer calls missing: {len(bad)} "
          f"({100*len(bad)/n if n else 0:.1f}%)")
    for v, q in bad[:3]:
        print(f"    claims '{v}' missing, but the question gives it: {q}")
    if bad:
        print("  FAIL: a refusal that fires on a satisfied precondition is wrong supervision.")
        return 1
    print("  PASS: every D1 refusal names a variable the question does not supply")
    return 0


if __name__ == "__main__":
    sys.exit(main())
