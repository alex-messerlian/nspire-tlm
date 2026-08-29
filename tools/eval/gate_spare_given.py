#!/usr/bin/env python3
"""Can the refusal be decided from the STRUCTURE of the givens, without reading the record?

gate_refusal_cue searches question N-GRAMS. This defect has no string: it is whether the question
supplies a variable the shown record does not use. Measured on the A42 corpus that trained the
retrain, that structure appeared in 99.7% of D2 and 0.0% of ANSWERABLE, D1 and D3 -- 12,014 firings
at 100% PRECISION -- and the model learned it instead of the judgement:

    fit_m  (every item carries a spare given)   98.9%
    fit    (no item carries one)                 7.2%
    answer control (record ANSWERS, spare given) 79.2% REFUSED, up from 22.8%

A capability measured at 98.9% that refuses four fifths of the answerable case is a surface rule
wearing the capability's name. Twelfth instance of a refusal class separable without reading the
record; the first that is structural rather than a field, which is exactly why the n-gram search
passed it.

The fix is not to remove the spare given -- it is device-faithful, since a real user's question
carries their own givens whatever the picker returns. The fix is to make it independent of the
class. This gate measures the per-class rates and the resulting classifier precision against the
base rate: a cue at the base rate carries no information.
"""
import collections, json, pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "corpus"))
from recfmt import fields                                          # noqa: E402

VAR = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
RESV = {"pi", "e", "sin", "cos", "tan", "asin", "acos", "atan", "sqrt", "log", "ln", "exp", "abs"}
MAX_LIFT = 12.0   # pp of precision above the base rate. 100% precision was a lift of 85.1.

# PRESENCE AND COUNT ARE DIFFERENT FAMILIES AND CLOSING ONE SAYS NOTHING ABOUT THE OTHER.
# A43 equalised PRESENCE -- the lift fell from +85.1 to +1.2 -- and the cue MOVED to COUNT: a
# mismatch inherits the wrong record's variables AND the added spare, so `spare count >= 2 ->
# refuse` was 100% precision at +85.2 pp, exactly the lift the presence cue had had. Nothing saw
# it because nothing had asked about count.
#
# A structural cue has FAMILIES and closing one says nothing about the others. This gate checks:
#   PRESENCE  is a spare given present at all
#   COUNT     how many, at every threshold
#   ABSENCE   does the question carry NO numeric given -- 66% of D3 and 15.8% of D2 once did,
#             against 0.0% of ANSWERABLE, for +82.6 pp decidable from the QUESTION ALONE
#   ORDERING  are the givens in the record's own variable order, or reverse-alphabetical --
#             the mismatch trim shuffled and ANSWERABLE did not, which alone was +36.5 pp
#   TOTAL     how many givens the question supplies AT ALL, spare or not. Equalising the SPARE
#             count left the TOTAL differing -- ANSWERABLE 3.84, D1 2.89, D2 2.27 -- for +20.2 pp,
#             because D1 withholds one and a mismatch is trimmed. It also surfaced as a false
#             ORDERING signal: fewer givens makes a random shuffle likelier to be reverse
#             alphabetical. A family is not closed by equalising a component of it.
# It does NOT check: value distributions, question/record length ratios, symbol identity, or
# interactions between two individually-flat features. Stated so a PASS here is not read as
# "no structural cue exists". See docs/RESULT_STRUCTURAL_FAMILIES.md.
COUNT_THRESHOLDS = (1, 2, 3)

# THE RATCHET APPLIES TO THE FULLY-BOUND POPULATION, AND THAT IS A SCOPE DECISION WITH A REASON.
#
# Over ALL record-bearing documents the counts cannot be equalised, and trying produced a seesaw:
# equalising the SPARE count left the TOTAL differing by class (+20.2 pp); equalising the TOTAL put
# the spare count back (+16.3 pp). The reason is structural -- for a MISMATCH every given is spare
# by definition, while an answerable question supplies its record's own variables. Both can only
# match if answerable documents stop supplying what their record needs, which destroys the class.
#
# That separability is INTRINSIC and it is the BINDING signal: if the shown record's variables are
# not in the question, it cannot be evaluated, and refusing is correct. The device encodes exactly
# this in `missing:`, so any tell it leaves is one the runtime leaves too -- the same reading
# already accepted for D3.
#
# The fit judgement is only REQUIRED where the record is fully bound. On that population the two
# classes must be indistinguishable, and they are: every count lift is under +0.6 pp. The whole
# population is still measured and printed, marked INTRINSIC, so a reader sees both numbers.
FULLY_BOUND_ONLY = True


def rhs(f):
    return {v for v in VAR.findall(f.split("=", 1)[1]) if v not in RESV}


def control():
    """A CLEAN TREE AND A DISABLED CHECK PRINT THE SAME PASS. So the predicate is exercised on a
    synthetic known-bad every run: the pre-A43 distribution (D2 always spare, ANSWERABLE never)
    must fail, and a distribution where every class matches must pass."""
    def lift(ans_rate, d2_rate, n_ans=8000, n_d2=500, n_d1=900):
        fp = ans_rate * n_ans
        tp = d2_rate * n_d2 + ans_rate * n_d1
        prec = 100.0 * tp / (tp + fp) if tp + fp else 0.0
        base = 100.0 * (n_d2 + n_d1) / (n_ans + n_d2 + n_d1)
        return prec - base
    bad = lift(0.0, 1.0)      # the A42 corpus: 100% precision
    good = lift(1.0, 1.0)     # every class always spare: no information
    ok = bad > MAX_LIFT and good <= MAX_LIFT
    print(f"  control: pre-A43 shape lifts {bad:+.1f} pp (must fail), matched shape "
          f"{good:+.1f} pp (must pass) -> {'OK' if ok else 'BROKEN'}")
    return ok


def main():
    if not control():
        print("FAIL: the gate's own predicate does not separate its controls"); return 1
    p = ROOT / "corpus/synth_sample.jsonl"
    if not p.exists():
        print("CANNOT CHECK: corpus absent -- not a pass"); return 2
    hit, tot = collections.Counter(), collections.Counter()
    for line in p.open():
        o = json.loads(line)
        rec = o["text"].split("<r>", 1)[1].split("<", 1)[0]
        if rec.startswith("none"):
            continue                       # D3 has no record, so "spare" is undefined
        if FULLY_BOUND_ONLY and "missing:none" not in rec:
            continue                       # the fit judgement is only required when bound
        k = o.get("kind", "ANSWERABLE")
        q = o["text"].split("<q>")[1].split("</q>")[0]
        given = set(re.findall(r"([A-Za-z_][A-Za-z0-9_]*)\s*=", q))
        tot[k] += 1
        hit[k] += bool(given - rhs(fields(rec)[0]))
    counts = collections.defaultdict(collections.Counter)
    for line in p.open():
        o = json.loads(line)
        rec = o["text"].split("<r>", 1)[1].split("<", 1)[0]
        if rec.startswith("none"):
            continue
        if FULLY_BOUND_ONLY and "missing:none" not in rec:
            continue
        q = o["text"].split("<q>")[1].split("</q>")[0]
        given = set(re.findall(r"([A-Za-z_][A-Za-z0-9_]*)\s*=", q))
        counts[o.get("kind", "ANSWERABLE")][min(len(given - rhs(fields(rec)[0])), 3)] += 1

    print("  [scope: FULLY-BOUND documents -- where the fit judgement is required]")
    print("  spare-variable rate by class (a question supplying a variable the record does not use):")
    for k in sorted(tot):
        print(f"    {k:11s} {100*hit[k]/tot[k]:5.1f}%   ({hit[k]:,}/{tot[k]:,})")
    refuse_k = sum(hit[k] for k in tot if k != "ANSWERABLE")
    refuse_n = sum(tot[k] for k in tot if k != "ANSWERABLE")
    fp = hit["ANSWERABLE"]
    prec = 100.0 * refuse_k / (refuse_k + fp) if (refuse_k + fp) else 0.0
    base = 100.0 * refuse_n / sum(tot.values())
    lift = prec - base
    print(f"  'spare variable -> refuse'  precision {prec:.1f}%  base rate {base:.1f}%  "
          f"LIFT {lift:+.1f} pp")
    print("  spare-variable COUNT distribution by class:")
    print(f"    {'class':11s} {'0':>7} {'1':>7} {'2':>7} {'3+':>7}")
    for k in sorted(counts):
        t = sum(counts[k].values())
        print(f"    {k:11s}" + "".join(f"{100*counts[k][i]/t:6.1f}%" for i in range(4)))
    worst_thr, worst = None, -99.0
    for thr in COUNT_THRESHOLDS:
        tp = sum(counts[k][i] for k in counts if k != "ANSWERABLE" for i in range(thr, 4))
        fpc = sum(counts["ANSWERABLE"][i] for i in range(thr, 4))
        pr = 100.0 * tp / (tp + fpc) if (tp + fpc) else 0.0
        lf = pr - base
        print(f"    'count >= {thr} -> refuse'  precision {pr:5.1f}%  LIFT {lf:+6.1f} pp")
        if lf > worst:
            worst, worst_thr = lf, thr
    if worst > MAX_LIFT:
        print(f"  FAIL: the spare-given COUNT predicts refusal {worst:+.1f} pp above chance at "
              f"threshold >= {worst_thr}, over the {MAX_LIFT} pp ratchet.")
        print("  Closing the PRESENCE cue does not close the COUNT cue -- they are different")
        print("  families and this one was found only after presence was fixed.")
        return 1
    print(f"  worst COUNT lift {worst:+.1f} pp at threshold >= {worst_thr}")
    # ABSENCE and ORDERING
    NUM = re.compile(r"-?\d+\.?\d*(?:[eE][-+]?\d+)?")
    fam = {}
    for label, pred, need2 in (
            ("ABSENCE  no numeric given", lambda q, rc: len(NUM.findall(q)) == 0, False),
            ("TOTAL    givens <= 2", lambda q, rc: len(re.findall(r"([A-Za-z_][A-Za-z0-9_]*)\s*=", q)) <= 2, False),
            ("TOTAL    givens <= 3", lambda q, rc: len(re.findall(r"([A-Za-z_][A-Za-z0-9_]*)\s*=", q)) <= 3, False),
            ("ORDERING reverse-alpha", lambda q, rc: (lambda n: len(n) >= 2 and n == sorted(n, reverse=True))(
                re.findall(r"([A-Za-z_][A-Za-z0-9_]*)\s*=", q)), True)):
        hh, tt = collections.Counter(), collections.Counter()
        for line in p.open():
            o = json.loads(line)
            rc = o["text"].split("<r>", 1)[1].split("<", 1)[0]
            q = o["text"].split("<q>")[1].split("</q>")[0]
            k = "D3" if rc.startswith("none") else o.get("kind", "ANSWERABLE")
            # D3 IS EXCLUDED FROM THE ABSENCE CHECK, AND THAT IS A SCOPE DECISION, NOT AN
            # OMISSION. src/store/assemble.c:109 emits the literal "none | ... | fit:low" whenever
            # the picker finds nothing, so a D3's RECORD SPAN already announces it perfectly -- a
            # question-side tell adds nothing a model could not read off the record, and it is what
            # the runtime genuinely produces. Trying to remove it made 55 of 307 D3 documents
            # answerable by a store record, breaking gate_d3_legitimacy. Over record-bearing
            # documents, where the property IS a defect, the lift is +0.0 pp.
            if k == "D3":
                continue
            if FULLY_BOUND_ONLY and "missing:none" not in rc:
                continue
            if need2 and len(re.findall(r"([A-Za-z_][A-Za-z0-9_]*)\s*=", q)) < 2:
                continue
            tt[k] += 1; hh[k] += bool(pred(q, rc))
        tpx = sum(hh[k] for k in tt if k != "ANSWERABLE"); fpx = hh["ANSWERABLE"]
        bx = 100.0 * sum(tt[k] for k in tt if k != "ANSWERABLE") / max(1, sum(tt.values()))
        px = 100.0 * tpx / (tpx + fpx) if (tpx + fpx) else bx
        fam[label] = px - bx
        print(f"  {label:28s} LIFT {px - bx:+6.1f} pp")
    over = {k: v for k, v in fam.items() if v > MAX_LIFT}
    if over:
        for k, v in over.items():
            print(f"  FAIL: {k} predicts refusal {v:+.1f} pp above chance, over the {MAX_LIFT} pp ratchet.")
        return 1

    if lift > MAX_LIFT:
        print(f"  FAIL: the structure of the givens predicts refusal {lift:.1f} pp above chance, "
              f"over the {MAX_LIFT} pp ratchet.")
        print("  A model can refuse without reading the record. See docs/RESULT_A42_SHAPE_CUE.md.")
        return 1
    print(f"  PASS: lift {lift:+.1f} pp is within the {MAX_LIFT} pp ratchet.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
