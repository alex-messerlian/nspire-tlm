#!/usr/bin/env python3
"""PERMANENT GATE: store_clean.json and units_train.json must declare the same units.

WHY. Two files carry units for the same relations. `units_train` is the hand-annotated set and the
generator prefers it; `store_clean` is what SHIPS TO THE DEVICE and is the output of a review. When
they disagree, a fix lands in one and the other silently overrides it.

MEASURED, and this gate exists because it happened while writing it: the Carnot and Fahrenheit
records declared their TEMPERATURES IN SECONDS. Corrected in `store_clean` -- and the generator went
on emitting `s`, because `units_train` still said so and wins. A correction that does not propagate
is indistinguishable from one never made.

Same class as docs/RESULT_STORE_CLEANING.md's cleaning reaching store_clean and nothing else, and
the same class as the three producers of one record format disagreeing.

WHAT IT DOES NOT VERIFY: that either file is RIGHT. dim_gate checks dimensional consistency against
the formula; this checks only that the two agree, so a unit wrong in both passes here.
"""
import json, pathlib, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]


def load(p):
    d = json.load(open(ROOT / p))
    if isinstance(d, list):
        return {r["f"]: (r.get("units") or {}) for r in d if r.get("f")}
    return {f: (r.get("units") or {}) for f, r in d.items()}


if __name__ == "__main__":
    store = load("corpus/store_clean.json")
    ann = load("corpus/units_train.json")
    shared = sorted(set(store) & set(ann))
    if not shared:
        # ABSENCE IS FAILURE: no overlap means the comparison ran on nothing and would report clean.
        print("CANNOT CHECK: store_clean and units_train share no relation"); sys.exit(2)

    bad = []
    for f in shared:
        for v in sorted(set(store[f]) & set(ann[f])):
            if store[f][v] != ann[f][v]:
                bad.append((f, v, store[f][v], ann[f][v]))
    print(f"  relations in both files      {len(shared)}")
    print(f"  UNIT DECLARATIONS DISAGREEING {len(bad)}")
    for f, v, a, b in bad[:10]:
        print(f"      {f[:34]:36} {v}: store={a!r}  units_train={b!r}")
    if bad:
        print("\n  FAIL: the two unit sources disagree. The generator prefers units_train, so a fix")
        print( "  applied only to store_clean never reaches the corpus -- and a correction that does")
        print( "  not propagate is indistinguishable from one never made. Fix both.")
        sys.exit(1)
    print("\n  PASS: both unit sources agree on every shared relation")
