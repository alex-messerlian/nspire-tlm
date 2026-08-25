"""PERMANENT GATE: the LHS of every record must be a single identifier.

The record contract is "LHS is the quantity solved for". `store.h` assumes it (`const char *lhs;
/* the quantity SOLVED FOR */`), the prompt assembler assumes it, and the runtime binds it as the
output variable. NOTHING HAS EVER VERIFIED IT.

Found by hand-check, not by any gate: `K*E=((1)/(2))*m*(v)^(2)` shipped as "kinetic energy".
`K*E` is not a name -- it PARSES AS K TIMES E, and the evaluator returns 6 for K=2, E=3. It looks
like physics notation and evaluates as arithmetic, and nothing in the pipeline distinguished those.
`B*E=h*f_O` had the same defect and was the first eval item ever inspected in this project.

Mechanically checkable, which is the point: this class needed a human to NOTICE, and needs no human
to ENFORCE. Exit 1 on any violation.

Does NOT verify: that the LHS is the RIGHT quantity, that it is spelled consistently with the
units map, or anything about the RHS."""
import json, re, sys

SIMPLE = re.compile(r"^[A-Za-z][A-Za-z0-9_]*$")

def violations(store):
    out = []
    for r in store:
        f = r.get("f", "")
        if "=" not in f:
            out.append((r, "no '=' -- not a relation")); continue
        lhs = f.split("=", 1)[0].strip()
        if not SIMPLE.match(lhs):
            kind = ("parses as a PRODUCT, not a name" if "*" in lhs else
                    "function-call form" if "(" in lhs else
                    "not a single identifier")
            out.append((r, f"LHS {lhs!r} {kind}"))
    return out

if __name__ == "__main__":
    path = sys.argv[1] if len(sys.argv) > 1 else "corpus/store_clean.json"
    store = json.load(open(path))
    bad = violations(store)
    for r, why in bad:
        print(f"VIOLATION {r['f'][:46]:48} {why}")
    print(f"{len(bad)} violation(s) across {len(store)} records in {path}")
    sys.exit(1 if bad else 0)
