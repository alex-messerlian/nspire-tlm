#!/usr/bin/env python3
"""PERMANENT GATE: a given must lie in the physical range of the quantity it stands for.

A17 (gate_plausible) filters RESULTS. Every INPUT was drawn from one empirical pool regardless of
what the variable is, so 69% of documents on the six trigonometric records fed an angle of, for
instance, 55.7 radians -- 8.9 full turns. `cos` of it is a number, the evaluator computes it, the
result is dimensionally impeccable and every gate passed. The scenario does not exist.

THE RANGE IS PER QUANTITY, NOT PER UNIT. The declared unit cannot decide it: `1` covers angles,
counts, quantum numbers, refractive indices and plain ratios, whose admissible ranges differ by
orders of magnitude. The class comes from the variable AND the record together -- and, for angles,
from WHERE THE VARIABLE APPEARS: a trig argument is an angle whatever it is called. A symbol list
was tried first and constrained `alpha` in `p=h/lambda`, which is not an angle.

WHAT IT DOES NOT VERIFY, and this is the honest half: only quantities in `quantity_range()`'s table
are checked. A variable it does not recognise is unconstrained, and MOST ARE. "Passes this gate"
means "no recognised quantity is out of range", never "the givens are physically sensible".
"""
import importlib.util, io, contextlib, pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
N, SEED, LIMIT = 3000, 999, 0.002
NUM = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(-?\d+(?:\.\d+)?(?:[eE][-+]?\d+)?)")

if __name__ == "__main__":
    spec = importlib.util.spec_from_file_location("gen", ROOT / "corpus/generate.py")
    m = importlib.util.module_from_spec(spec)
    try:
        with contextlib.redirect_stdout(io.StringIO()):
            spec.loader.exec_module(m)
    except SystemExit:
        pass
    out = m.gen(N, seed=SEED)
    docs = [d["text"] if isinstance(d, dict) else d for d in (out[0] if isinstance(out, tuple) else out)]
    by_f = {r["f"]: r for r in m.recs}

    checked, bad, examples = 0, 0, []
    for t in docs:
        f = re.search(r"<r>([^|]+)\|", t)
        q = re.search(r"<q>(.*?)</q>", t)
        if not (f and q): continue
        rec = by_f.get(f.group(1).strip())
        if rec is None: continue
        for var, val in NUM.findall(q.group(1)):
            rr = m.quantity_range(rec, var)
            if rr is None: continue
            checked += 1
            v = float(val)
            if not (rr[0] <= v <= rr[1]) or (rr[2] and not v.is_integer()):
                bad += 1
                if len(examples) < 5:
                    examples.append((var, val, (rec.get("name") or "")[:34], rr[3]))
    if not checked:
        # ABSENCE IS FAILURE: a table that recognised nothing would report a perfect zero.
        print("CANNOT CHECK: no given matched a quantity in the range table"); sys.exit(2)

    # POSITIVE CONTROL in-run: the table must be able to fire.
    probe = {"f": "y=x*sin(theta)", "name": "probe", "units": {"theta": "1", "x": "m", "y": "m"}}
    rr = m.quantity_range(probe, "theta")
    if rr is None or 55.7 <= rr[1]:
        print("\n  FAIL: the positive control was not caught -- an angle of 55.7 rad is accepted.")
        sys.exit(1)

    rate = bad / checked
    print(f"  givens with a known range    {checked:,}")
    print(f"  OUT OF PHYSICAL RANGE        {bad}  ({rate:.2%}, limit {LIMIT:.2%})")
    for var, val, nm, why in examples: print(f"      {var}={val} in {nm!r} -- {why}")
    print(f"  positive control             55.7 rad as an angle IS rejected")
    if rate > LIMIT:
        print(f"\n  FAIL: {rate:.2%} of recognised givens are outside the range their quantity allows.")
        print( "  The arithmetic is right and the scenario does not exist; no other gate sees this.")
        sys.exit(1)
    print("\n  PASS: every recognised given is in range")
