"""PERMANENT LINT: Leibniz notation written as multiplication.

`dV/dt` encoded as `(d*V)/(d*t)` parses as d×V ÷ d×t. The `d` CANCELS, so the value is right by
accident — and the runtime, which builds its variable prompts from the formula, asks the student
for a value of `d`. Under E that is a visible interface defect, not a silent one.

Same class as `K*E` on the left-hand side: notation that reads as physics and evaluates as
arithmetic. `K*E` was caught by the LHS gate; this is the right-hand-side form of it.

Detection is the CANCELLING shape specifically — `d*` in both numerator and denominator — not any
`d*X`. A bare `d*X` is usually a genuine variable: `Delta_l = d*sin(theta)` has `d` as slit
separation and is correct. Narrowing the claim to the shape is what gives the lint precision; the
broad version flagged that record as a false positive.

Does NOT verify: derivatives written any other way, integrals, or whether a finite-difference
rewrite is physically appropriate — that is a judgement call for a human.
"""
import json, re, sys

NUM_DEN = re.compile(r"\(\s*(?:-\s*)?\(?\s*d\s*\*\s*[A-Za-z][A-Za-z0-9_]*\s*\)?\s*\)"
                     r"\s*/\s*"
                     r"\(\s*\(?\s*d\s*\*\s*[A-Za-z][A-Za-z0-9_]*\s*\)?\s*\)")

def violations(store):
    out = []
    for r in store:
        if "f" not in r:
            out.append((r, "record has no formula field -- absence is a FAILURE here, not a skip"))
            continue
        f = r["f"].replace(" ", "")
        # d* appearing on both sides of a division, at any nesting
        if re.search(r"d\*[A-Za-z]", f):
            parts = re.split(r"\)/\(", f)
            if len(parts) >= 2 and re.search(r"d\*[A-Za-z]", parts[0]) and \
               any(re.search(r"d\*[A-Za-z]", p) for p in parts[1:]):
                out.append((r, "d appears as a multiplier in BOTH numerator and denominator "
                               "-- Leibniz notation, and d cancels"))
    return out

if __name__ == "__main__":
    store = json.load(open(sys.argv[1] if len(sys.argv) > 1 else "corpus/store_clean.json"))
    bad = violations(store)
    for r, why in bad:
        print(f"LEIBNIZ {r['rid']}  {r['f'][:44]:46} {why}")
        print(f"         declares d:{(r.get('units') or {}).get('d','?')!r} "
              f"-- a unit for the differential operator, which is itself the tell")
    print(f"{len(bad)} violation(s) across {len(store)} records")
    sys.exit(1 if bad else 0)
