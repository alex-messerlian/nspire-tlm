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
        k = o.get("kind", "ANSWERABLE")
        q = o["text"].split("<q>")[1].split("</q>")[0]
        given = set(re.findall(r"([A-Za-z_][A-Za-z0-9_]*)\s*=", q))
        tot[k] += 1
        hit[k] += bool(given - rhs(fields(rec)[0]))
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
    if lift > MAX_LIFT:
        print(f"  FAIL: the structure of the givens predicts refusal {lift:.1f} pp above chance, "
              f"over the {MAX_LIFT} pp ratchet.")
        print("  A model can refuse without reading the record. See docs/RESULT_A42_SHAPE_CUE.md.")
        return 1
    print(f"  PASS: lift {lift:+.1f} pp is within the {MAX_LIFT} pp ratchet.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
