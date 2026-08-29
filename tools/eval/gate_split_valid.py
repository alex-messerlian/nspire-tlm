#!/usr/bin/env python3
"""PERMANENT GATE: no split item may be one the runtime could never produce.

WHY. `answer_ok` was reported at 16% on the trained checkpoint and it was measuring the SPLIT, not
the model — the fourth headline number in this project that turned out to be its instrument. Two
independent defects, both in corpus/split_select.json:

  * 10% of `units_holdout.json` is MathML conversion garbage that the store cleaning removed from
    store_clean.json and never from the holdout — two Leibniz artifacts where `d` cancels, and one
    fused identifier. The split draws half the holdout, so they were over-represented.
  * 32% of answer items supplied a value for a variable the runtime INLINES as a physical constant
    (`c = 2` for the speed of light, `h = 2` for Planck's), then scored the model wrong for
    correctly inlining c = 2.998e8.

Held-out formulas are NOT a defect and this gate must not flag them: SELECT deliberately uses
records the model never trained on, because the architecture requires reading the record from the
prompt rather than from memory. What is checked is that an item is one the DEVICE could emit.
"""
import json, pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
LEIBNIZ = re.compile(r"\(d\*[A-Za-z_]")
FUSED   = re.compile(r"^[A-Z]\*[A-Z][A-Za-z0-9_]*=")
GIVEN   = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)\s*=\s*-?[\d.]+(?:[eE][-+]?\d+)?")


def main():
    store = json.loads((ROOT / "corpus/store_clean.json").read_text())
    store = store if isinstance(store, list) else store.get("records", [])
    hold  = json.loads((ROOT / "corpus/units_holdout.json").read_text())
    cval  = {}
    for r in list(store) + list(hold):
        for k in (r.get("cval") or {}):
            cval.setdefault(r.get("f"), set()).add(k)

    bad = []
    n = 0
    for name in ("split_select.json", "split_report.json"):
        p = ROOT / "corpus" / name
        if not p.exists():
            print(f"  CANNOT CHECK: {name} is missing. Refusing to report a pass.")
            return 2
        for it in json.loads(p.read_text()):
            n += 1
            f = it["record"].split("|")[0].strip()
            if LEIBNIZ.search(f):
                bad.append((name, it["id"], f, "Leibniz artifact: `d` cancels, not a relation"))
            if FUSED.match(f):
                bad.append((name, it["id"], f, "fused identifier from the MathML conversion"))
            supplied = set(GIVEN.findall(it["q"]))
            clash = supplied & cval.get(f, set())
            if clash:
                bad.append((name, it["id"], f,
                            f"supplies {sorted(clash)}, which the runtime inlines as a constant"))
    print(f"  split items checked: {n}")
    for name, iid, f, why in bad[:10]:
        print(f"  INVALID  {name} {iid}  {f}\n           {why}")
    if bad:
        print(f"\n  FAIL: {len(bad)} split item(s) the runtime could never produce. Scoring a model")
        print("  on them measures the split. Rebuild with corpus/build_splits.py.")
        return 1
    if n == 0:
        print("  CANNOT CHECK: the splits are empty. Not a pass.")
        return 2
    print("  PASS: every split item is one the device could emit")
    return 0


if __name__ == "__main__":
    sys.exit(main())
