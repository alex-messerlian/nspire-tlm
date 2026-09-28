"""PERMANENT LINT: defects self-evident from a record's own declaration.

The Leibniz find was surfaced by the DATA, not by the formula: a record declaring `d: 'm'` is
declaring a unit for the differential operator, which is self-evidently wrong on its face. That
shape -- an internal contradiction visible without any physics, any evaluator, and any reference --
is the cheapest gate available. This collects every one of that shape.

Each check needs only the record's own fields. None needs a source page, a dictionary, or a
judgement about physics.

Does NOT verify: that a consistently-declared record is physically correct. A record can be
internally flawless and still state a false relation -- `R_eqv=R_1-R_2` passes every check here.
"""
import json, re, sys

VAR = re.compile(r"(?<![A-Za-z0-9_])([A-Za-z][A-Za-z0-9_]*)(?![A-Za-z0-9_(])")
RESV = {"pi","e","sin","cos","tan","ln","log","sqrt","exp","asin","acos","atan"}
# NO OPERATORS SET. "declares a unit for d" is NOT the Leibniz tell: `d` is legitimately
# distance in 9 records (v=d/t, W=F*d, C=epsilon_0*A/d). The tell is d declared AND d cancelling,
# which is a property of the FORMULA and belongs in lint_leibniz.py. Tried the declaration-only
# version, it flagged 9 correct records, removed. A declaration-only check must be decidable from
# the declaration ALONE -- that is the whole point of this file.

def check(r):
    out = []
    f = r.get("f","")
    if "=" not in f: return [("no-equation", f"{f!r} is not a relation")]
    lhs, rhs = f.split("=",1); lhs = lhs.strip()
    units = r.get("units") or {}
    rhs_vars = {v for v in VAR.findall(rhs) if v not in RESV}
    all_vars = rhs_vars | {lhs}

    for v in sorted(units):
        if v in RESV:
            out.append(("unit-for-reserved", f"declares reserved name {v!r} as a variable"))
        if v not in all_vars:
            out.append(("declared-unused", f"declares {v!r} which does not appear in the formula"))
    for v in sorted(rhs_vars):
        if v not in units:
            out.append(("undeclared", f"uses {v!r} with no declared unit"))
    if lhs not in units:
        out.append(("lhs-undeclared", f"solved-for {lhs!r} has no declared unit"))
    for v, c in (r.get("cval") or {}).items():
        if c and v not in all_vars:
            out.append(("const-unused", f"constant for {v!r}, absent from the formula"))
        if c and v == lhs:
            out.append(("const-for-lhs", f"constant supplied for the SOLVED-FOR variable {v!r}"))
    nm = r.get("name","")
    if not nm.strip():
        out.append(("no-name", "empty name -- under E the name is the interface"))
    else:
        # NAME HYGIENE. Mining artefacts: 'Tangential speed:' kept a trailing colon from the
        # source heading. Under E the name IS the interface, so these are user-facing defects
        # and every one is decidable from the name alone.
        if re.search(r"[:;,\-\u2013\u2014]\s*$", nm):
            out.append(("name-trailing-punct", f"{nm!r} ends in punctuation"))
        if re.search(r"^\s*[:;,]", nm):   out.append(("name-leading-punct", f"{nm!r}"))
        if "  " in nm:                     out.append(("name-double-space", f"{nm!r}"))
        if nm != nm.strip():               out.append(("name-untrimmed", f"{nm!r}"))
        if nm.count("(") != nm.count(")"): out.append(("name-unbalanced-paren", f"{nm!r}"))
        if re.search(r"\b(?:with|for|of|in|and|to|from|the|a|an)\s*$", nm, re.I):
            out.append(("name-truncated", f"{nm!r} ends mid-clause -- mined text cut short"))
    return out

if __name__ == "__main__":
    store = json.load(open(sys.argv[1] if len(sys.argv) > 1 else "corpus/store_clean.json"))
    import collections
    kinds = collections.Counter(); n = 0
    for r in store:
        for kind, why in check(r):
            print(f"{kind:18} {r['rid']}  {r['f'][:36]:38} {why}")
            kinds[kind] += 1; n += 1
    print(f"\n{n} finding(s) across {len(store)} records")
    for k, c in kinds.most_common(): print(f"  {k:18} {c}")
    sys.exit(1 if n else 0)
