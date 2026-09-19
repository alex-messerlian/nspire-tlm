#!/usr/bin/env python3
"""PERMANENT GATE: the EXPLAIN tier must carry ONE written answer per record, not three.

THIS GATE EXISTS BECAUSE ITS ABSENCE COST A TRAINING RUN. A82 measured the effect on a
single-variable change with everything else identical:

                           3 variants   1 variant
    prose, right relation      74.4%       94.5%
    FABRICATED a relation      23.2%        3.7%

One explanation seen ~29 times beats three seen ~10 times each. The mechanism is exposures per
STRING: an EXPLAIN answer appears nowhere in its prompt, so it has to be memorised whole, and
splitting the budget three ways memorises none of them.

A82 then left the measured-better value behind an environment variable WHOSE DEFAULT WAS THE BROKEN
ONE. A107 regenerated without setting it and reproduced the defect to within a point -- 22.0%
fabrication, D2 prose 72.9% below its floor, D1 4/6 against a pre-registered 5.

SO THE CHECK READS THE CORPUS, NOT A STAMP. A stamp records what the generator was told; this counts
what it emitted, which cannot drift from itself.

TWO NORMALISATIONS, AND THE SECOND IS THE WHOLE DIFFICULTY. Case jitter flips the first letter of
about half the questions. A51 then SCRAMBLES SYMBOLS in ~24% of documents, so one written string
appears as many distinct strings:

    a wave travels one whole wavelength in one cycle, so v = f*lambda is really just ...
    a wave travels one whole wavelength in one cycle, so rho = beta*eta is really just ...

The first version of this gate counted those as variants and reported a median of 8 on a corpus
generated at 1 -- it would have sent me to regenerate a corpus that was already correct. It is the
proxy-predicate class again, written into the very gate meant to stop a decision lapsing, and the
control is what caught it: the control covered case jitter and not scrambling, so it passed a gate
that was wrong about three quarters of its live surface.

The renaming is undone EXACTLY rather than guessed at. A scrambled document carries its own key: the
record span holds the renamed formula and `head` holds the canonical one, identical in structure, so
aligning their identifiers left to right gives the inverse map. Where that alignment does not hold
the document is COUNTED AS UNCHECKABLE and the gate fails on it -- "cannot check" does not share an
exit status with "checked and clean".
"""
import collections, json, pathlib, re, statistics, sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / 'corpus'))
from recfmt import formula as _rf_formula  # " | " is the separator; a formula may contain a
# bare pipe. `f_beat=|f_2-f_1|` is a shipped record and a bare-pipe split truncates it to
# `f_beat=`, which this gate then reported as 8 uncheckable documents -- its own extractor,
# not the corpus. Third instrument fault in one gate, after the case jitter and the scrambler.

ROOT = pathlib.Path(__file__).resolve().parents[2]
CORPUS = ROOT / "corpus/synth_sample.jsonl"
MAX_MEDIAN = 1          # A82's measured value. 3 is the arm that fabricates.
IDENT = re.compile(r"[A-Za-z_][A-Za-z_0-9]*")


def unscramble(ans, shown, head):
    """Map the document's symbols back to canonical ones. None if the alignment does not hold."""
    a, b = IDENT.findall(shown), IDENT.findall(head)
    if len(a) != len(b):
        return None
    m = {}
    for x, y in zip(a, b):
        if m.setdefault(x, y) != y:       # one symbol standing for two is not a renaming
            return None
    return IDENT.sub(lambda g: m.get(g.group(0), g.group(0)), ans)


def variants(lines):
    per, bad = collections.defaultdict(set), 0
    for o in lines:
        if o.get("kind") != "EXPLAIN" or "<a>" not in o["text"]:
            continue
        t = o["text"]
        ans = t.split("<a>", 1)[1].replace("<end>", "").strip()
        shown = _rf_formula(t.split("<r>", 1)[1].split("<a>", 1)[0])
        if not ans:
            continue
        canon = unscramble(ans, shown, o.get("head", "")) if o.get("scrambled") else ans
        if canon is None:
            bad += 1
            continue
        per[o.get("head")].add(canon[0].lower() + canon[1:])   # then the case jitter
    return per, bad


def real_known_bad():
    """The known-bad built from REAL strings, not invented ones.

    Regenerating at EXPLAIN_VARIANTS=3 costs ~18 minutes, which is far outside a gate's budget and
    is exactly the kind of cost that gets a check skipped. But the three variants themselves are
    shipped data: corpus/knowledge/explanations.json holds 531 of them across 177 records, and they
    are the very strings the broken arm would have emitted. So the control is assembled from those
    at JSON-load cost, and it is a plausible input rather than an invented one.
    """
    f = ROOT / "corpus/knowledge/explanations.json"
    if not f.exists():
        return None
    for rec in json.load(f.open()):
        vs = rec.get("variants") or []
        if len(vs) >= 3:
            h = rec["formula"]
            return [{"kind": "EXPLAIN", "head": h, "scrambled": False,
                     "text": f"<q>x</q><r>{h} | u<a>{v}<end>"} for v in vs[:3]]
    return None


def controls():
    """Both directions, and the scrambled direction is the one the first version got wrong."""
    doc = lambda a, r, h, s: {"kind": "EXPLAIN", "head": h, "scrambled": s,
                              "text": f"<q>x</q><r>{r} | u<a>{a}<end>"}
    plain = "Speed v is f times lambda here."
    scram = "Speed rho is beta times eta here."
    one = [doc(plain, "v=f*lambda", "v=f*lambda", False),
           doc(plain.lower(), "v=f*lambda", "v=f*lambda", False),
           doc(scram, "rho=beta*eta", "v=f*lambda", True)]      # same sentence, renamed
    three = one + [doc("A second thing entirely.", "v=f*lambda", "v=f*lambda", False),
                   doc("A third thing entirely.", "v=f*lambda", "v=f*lambda", False)]
    p1, _ = variants(one)
    p3, _ = variants(three)
    n1 = max(len(v) for v in p1.values())
    n3 = max(len(v) for v in p3.values())
    if n1 != 1:
        return f"a scrambled rendering of one sentence reads as {n1} variants, not 1"
    if n3 != 3:
        return f"three genuinely different sentences read as {n3}, not 3"
    if variants([doc(scram, "rho=beta*eta*tau", "v=f*lambda", True)])[1] != 1:
        return "a record span that does not align with its head was not counted uncheckable"
    rb = real_known_bad()
    if rb is None:
        return "explanations.json absent or carries no record with 3 variants: the real known-bad "
    n = max(len(v) for v in variants(rb)[0].values())
    if n != 3:
        return f"three REAL shipped variants of one record read as {n}, not 3"
    return None


def main():
    if not CORPUS.exists():
        print("CANNOT CHECK: corpus/synth_sample.jsonl absent. Not a pass.")
        return 2
    err = controls()
    if err:
        print(f"  CONTROL BROKEN: {err}")
        return 2
    print("  controls: case jitter and A51 scrambling both collapse to one variant; "
          "three distinct sentences stay three; an unalignable span counts as uncheckable; "
          "and three REAL shipped variants of one record read as three")

    per, bad = variants(json.loads(l) for l in CORPUS.open())
    if not per:
        print("  NOTHING TO CHECK: no EXPLAIN documents in the corpus. Not a pass.")
        return 2
    counts = sorted(len(v) for v in per.values())
    med = statistics.median(counts)
    print(f"  {len(per)} records carry EXPLAIN documents; {bad} documents uncheckable")
    print(f"  written answers per record, scrambling undone: "
          f"median {med:.0f}, min {counts[0]}, max {counts[-1]}")
    if bad:
        print(f"\n  FAIL: {bad} EXPLAIN documents whose record span does not align with their "
              f"head, so their symbols cannot be mapped back and their variant count is unknown.")
        return 1
    if med > MAX_MEDIAN:
        for f, v in sorted(per.items(), key=lambda kv: -len(kv[1]))[:3]:
            print(f"    {len(v)} variants on {f}")
        print(f"\n  FAIL: median {med:.0f} written answers per record, above {MAX_MEDIAN}. A82 "
              f"measured 3 variants at 23.2% fabrication against 1 at 3.7%; A107 reproduced that "
              f"at 22.0% and lost a training run. Regenerate with the default, or set "
              f"EXPLAIN_VARIANTS=3 deliberately to rebuild the old arm.")
        return 1
    print(f"\n  PASS: one written answer per record, the arm A82 measured at 3.7% fabrication "
          f"against 23.2%")
    return 0


if __name__ == "__main__":
    sys.exit(main())
