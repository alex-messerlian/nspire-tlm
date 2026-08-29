#!/usr/bin/env python3
"""Carve the SELECT/REPORT holdout OUT of the cleaned store, deterministically.

WHY. corpus/build_splits.py claimed "15 held-out formulas each, disjoint (never trained on)" and
nothing asserted it: 21 of the 30 records in units_holdout.json were also in the store, so 10 of
SELECT's 13 formulas appeared VERBATIM in the training corpus. SELECT was an IN-DISTRIBUTION test
presented as a generalisation test.

A file named "holdout" does not hold anything out. The only construction that makes "never trained
on" true is to choose the holdout FROM the store and then exclude it from generation, which is what
this does. corpus/generate.py reads the same file and skips those formulas, so the two cannot drift.

*** CORRECTED. The first version carved the holdout OUT of the shipped store, and gate_store_coverage
caught it immediately: that breaks this project's core corpus invariant, R_train includes R_store.
The device's picker can return ANY store record, and an untrained relation scores 12.2% against
41.0% for a trained one -- so holding out a shipped record trades the model's real quality for a
measurement. The gate was right and the carve was wrong.

The holdout must therefore be relations that are VALID PHYSICS, NOT IN THE SHIPPED STORE (so the
device can never retrieve them) and NEVER GENERATED (so the model never trains on them). 57 such
records exist in units_train.json. Several are near-duplicates of store relations under different
notation -- c=lambda*f against v=f*lambda, Delta_E_int=Q-W against Delta_U=Q-W -- which makes them
BETTER probes, not worse: the model must read the record in front of it rather than pattern-match a
familiar shape.

SIZE. 26 records, 13 per split, matching what build_splits already uses. It now costs nothing in
relation coverage, because nothing is taken away from training.

SELECTION. Seeded and stratified by variable count, so the holdout is not accidentally all the
simple two-variable relations, which would make the generalisation test easier than the task.
"""
import json, pathlib, random, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
VAR = re.compile(r"(?<![A-Za-z0-9_])([A-Za-z][A-Za-z0-9_]*)(?![A-Za-z0-9_(])")
RESV = {"pi", "e", "sin", "cos", "tan", "ln", "log", "sqrt", "exp"}
N_HOLDOUT = 26


def main():
    shipped = json.loads((ROOT / "corpus/store_clean.json").read_text())
    shipped = shipped if isinstance(shipped, list) else shipped.get("records", [])
    shipped_f = {r["f"] for r in shipped}
    pool = json.loads((ROOT / "corpus/units_train.json").read_text())
    pool = pool if isinstance(pool, list) else pool.get("records", [])
    LEIB  = re.compile(r"\(d\*[A-Za-z_]")
    FUSED = re.compile(r"^[A-Z]\*[A-Z][A-Za-z0-9_]*=")
    # Group 1 of the store cleaning: sequence recurrences and plane geometry are not physics, and a
    # generalisation probe made of them would measure the wrong thing.
    SEQ   = re.compile(r"a_n|a_1|\bn!|_n1|_n2")
    GEOM  = re.compile(r"^(A=pi\*a\*b|d_2=|A=\(\(1\)/\(2\)\)\*d_1)")
    def ok(r):
        f = r.get("f", "")
        return (bool(r.get("units")) and f and f not in shipped_f
                and not LEIB.search(f) and not FUSED.match(f)
                and not SEQ.search(f) and not GEOM.match(f))
    store = [r for r in pool if ok(r)]
    def nvars(r):
        rhs = r["f"].split("=", 1)[1]
        return len({v for v in VAR.findall(rhs)} - RESV)
    buckets = {}
    for r in store:
        buckets.setdefault(min(nvars(r), 4), []).append(r)
    rng = random.Random(20260828)
    for b in buckets.values():
        b.sort(key=lambda r: r["f"]); rng.shuffle(b)
    # proportional stratified draw, so the holdout mirrors the store's complexity mix
    out, total = [], len(store)
    for k in sorted(buckets):
        want = max(1, round(N_HOLDOUT * len(buckets[k]) / total))
        out += buckets[k][:want]
    out = out[:N_HOLDOUT]
    print(f"  shipped store        : {len(shipped)} records (untouched -- R_train includes R_store)")
    print(f"  eligible out-of-store: {len(store)} valid physics relations never shipped")
    print(f"  drawn into holdout   : {len(out)}")
    print(f"  complexity mix (free vars): holdout "
          f"{sorted(nvars(r) for r in out)}")
    (ROOT / "corpus/units_holdout.json").write_text(json.dumps(out, indent=1))
    print(f"  wrote corpus/units_holdout.json")
    return 0


if __name__ == "__main__":
    sys.exit(main())
