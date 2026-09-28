#!/usr/bin/env python3
"""PERMANENT GATE: no generated D3 document may decline a question the store answers.

THE CRITERION, stated before it was applied (see tools/eval/d3_criterion.py):

    A D3 document is LEGITIMATE iff the store contains NO record computing the quantity the
    question asks for from the givens the question supplies, plus resolved constants.

MEASURED: 843 of 843 generated D3 documents were ILLEGITIMATE. 100.0%, and a rate of exactly 100%
is the signature of an INABILITY -- generate.py builds the question FROM a real record and only then
overwrites the record span with the no-match literal, so a legitimate D3 is unreachable from that
path. The class was not mis-tuned; it could not be produced correctly.

POSITIVE CONTROL, which is what makes the 100% trustworthy rather than a broken predicate: the six
hand-written out-of-scope questions in GOOD_D3 below (ladder statics, two-block friction,
headwind projectile, three-cable statics) all score LEGITIMATE. The criterion discriminates.

This gate therefore has two jobs, and the second is why it is not just an assertion that the count
is zero: if a future source of genuinely out-of-scope questions appears, D3 may be generated again,
and each one must pass the criterion.
"""
import importlib.util, json, pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]


GOOD_D3 = [  # questions no stored relation answers, written by hand for the evaluation design
    "A ladder leans against a frictionless wall at 60 degrees. Find the friction force at the base.",
    "A block slides down a rough incline, compresses a spring, and rebounds. Find the final height.",
    "Two blocks connected over a pulley with friction on both surfaces. Find the acceleration.",
    "A projectile is launched at 40 degrees into a headwind. Find the range.",
    "A rod pivots about one end while a mass slides along it. Find the angular acceleration.",
    "Find the tension in each of three cables supporting a sign at different angles.",
]


def main():
    spec = importlib.util.spec_from_file_location("d3", ROOT / "tools/eval/d3_criterion.py")
    d3 = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(d3)
    recs = d3.load_store()

    corpus = ROOT / "corpus/synth_sample.jsonl"
    if not corpus.exists():
        print("  CANNOT CHECK: corpus/synth_sample.jsonl is missing. Refusing to report a pass.")
        return 2

    # POSITIVE CONTROL FIRST: if the criterion cannot clear the known-good questions it discriminates
    # nothing, and a zero count below would mean nothing either.
    good = GOOD_D3
    cleared = sum(1 for q in good
                  if not d3.answerable_by(recs, set(d3.GIVEN.findall(q)), q.lower()))
    print(f"  positive control: {cleared}/{len(good)} hand-written out-of-scope questions LEGITIMATE")
    if not good or cleared != len(good):
        print("  CANNOT CHECK: the criterion does not clear known-good questions, so a clean")
        print("  corpus result would prove nothing. Refusing to report a pass.")
        return 2

    total = bad = 0
    examples = []
    for i, line in enumerate(corpus.open()):
        t = json.loads(line)["text"]
        if "no matching relation" not in t:
            continue
        total += 1
        m = re.search(r"<q>(.*?)</q>", t, re.S)
        if not m:
            continue
        q = m.group(1)
        hits = d3.answerable_by(recs, set(d3.GIVEN.findall(q)), q.lower())
        if hits:
            bad += 1
            if len(examples) < 5:
                examples.append((i, q[:70], hits[0][1]))
    print(f"  D3 documents in corpus: {total}")
    for i, q, f in examples:
        print(f"  ILLEGITIMATE line {i}: {q}\n                 answered by {f}")
    if bad:
        print(f"\n  FAIL: {bad} of {total} D3 documents decline a question the store answers.")
        print("  That is wrong supervision -- generate.py's own A12 note: a refusal that fires on a")
        print("  satisfied precondition teaches the model to refuse a question it can answer.")
        return 1
    print(f"  PASS: {total} D3 document(s), none declining an answerable question")
    return 0


if __name__ == "__main__":
    sys.exit(main())
