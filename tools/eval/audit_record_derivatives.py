#!/usr/bin/env python3
"""AUDIT: every artefact derived from the record set, checked against the CLEANED store.

WHY. The store cleaning deleted 34 records -- 10 non-physics, 4 physically wrong (R_eqv = R_1 - R_2
shipped), 13 duplicates, 7 worked-example-specific. It reached store_clean.json and then had to be
propagated by hand, and it was missed three times:

  1. units_train.json  -- the generator's units authority; fixed after the corpus kept emitting `s`
                          for a temperature
  2. build/store.tns   -- the artefact the DEVICE reads; every store-derived gate was checking a
                          store nobody had rebuilt
  3. units_holdout.json -- the split source; put two Leibniz artifacts and a fused identifier into
                          the split the SELECTION RULE reads, and produced a retracted 16%

Three instances of one correction failing to propagate is not three lapses, it is a missing audit.
This is that audit: it enumerates every tracked JSON that names formulas and reports which reference
records the cleaned store does not have.

NOT EVERY HIT IS A DEFECT, and the report says which is which:
  * units_holdout.json legitimately holds records the store does not -- it is the HELD-OUT set, and
    SELECT deliberately uses formulas the model never trained on.
  * raw/mined artefacts (records_raw, frames_raw, batch*) are inputs to the cleaning, not outputs.
The failure mode is a DERIVED artefact -- one a consumer treats as current -- silently keeping
records the cleaning removed.
"""
import json, pathlib, re, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
FORMULA = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*\s*=\s*\S")

# Artefacts that legitimately contain non-store formulas, with the reason.
EXPECTED = {
    "corpus/units_holdout.json": "the HELD-OUT set: SELECT uses formulas the model never trained on",
    "corpus/records_raw.json":   "raw mined input to the cleaning, not an output",
    "corpus/units_raw.json":     "raw mined input",
    "corpus/aligned_raw.json":   "raw alignment, superseded and retained for provenance",
}


def store_formulas():
    d = json.loads((ROOT / "corpus/store_clean.json").read_text())
    d = d if isinstance(d, list) else d.get("records", [])
    return {r["f"] for r in d if r.get("f")}


def walk(o, out):
    if isinstance(o, dict):
        for k, v in o.items():
            if k in ("f", "formula", "record") and isinstance(v, str):
                cand = v.split("|")[0].strip()
                if FORMULA.match(cand): out.add(cand)
            walk(v, out)
    elif isinstance(o, list):
        for v in o: walk(v, out)


def main():
    store = store_formulas()
    files = subprocess.run(["git", "ls-files", "*.json"], cwd=ROOT,
                           capture_output=True, text=True).stdout.split()
    rows, unexpected = [], 0
    for rel in files:
        p = ROOT / rel
        try:    data = json.loads(p.read_text())
        except Exception:  continue
        found = set(); walk(data, found)
        if not found: continue
        missing = sorted(f for f in found if f not in store)
        if not missing: continue
        why = EXPECTED.get(rel)
        rows.append((rel, len(found), missing, why))
        if why is None: unexpected += 1
    print(f"  tracked JSON naming formulas: {len(rows)} reference records absent from the clean store\n")
    for rel, n, missing, why in sorted(rows, key=lambda r: (r[3] is not None, r[0])):
        tag = "EXPECTED" if why else "UNEXPLAINED"
        print(f"  {tag:11} {rel}   {len(missing)} of {n} not in store")
        if why: print(f"              reason: {why}")
        else:   print(f"              {', '.join(missing[:4])}{' ...' if len(missing) > 4 else ''}")
    if unexpected:
        print(f"\n  FAIL: {unexpected} artefact(s) keep records the cleaning removed and are not")
        print("  declared as raw inputs or held-out sets. Either re-derive them from the cleaned")
        print("  store, or add an EXPECTED entry saying why they legitimately differ.")
        return 1
    print("\n  PASS: every artefact naming a non-store record is a declared raw input or held-out set")
    return 0


if __name__ == "__main__":
    sys.exit(main())
