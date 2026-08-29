import sys
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
sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[2] / 'corpus'))
from recfmt import fields as _rf_fields, formula as _rf_formula  # " | " is the separator; a formula may contain a bare pipe

ROOT = pathlib.Path(__file__).resolve().parents[2]
FORMULA = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*\s*=\s*\S")

# Artefacts that legitimately contain non-store formulas, with the reason.
EXPECTED = {
    # --- the holdout and the splits built from it -------------------------------------------------
    "corpus/units_holdout.json": "the HELD-OUT set: valid physics deliberately NOT shipped and never "
                                 "trained, so SELECT tests reading the record from the prompt",
    "corpus/split_select.json":  "built from the holdout by construction",
    "corpus/split_report.json":  "built from the holdout by construction",
    # --- raw mined inputs to the cleaning, not outputs of it --------------------------------------
    "corpus/records_raw.json":   "raw mined input to the cleaning",
    "corpus/units_raw.json":     "raw mined input",
    "corpus/aligned_raw.json":   "raw alignment, superseded, retained for provenance",
    "corpus/examples_raw.json":  "1,910 mined worked examples, the pool the store was cut from",
    "corpus/examples_gated.json":"the gated subset of examples_raw, still pre-cleaning",
    "corpus/batch2.json":        "mining batch 2, under review when the cleaning ran",
    "corpus/batch2_usable.json": "batch 2 triage",
    "corpus/batch3.json":        "mining batch 3",
    "corpus/batch3_buckets.json":"batch 3 triage",
    "corpus/batch3_judgment.json":"batch 3 review verdicts",
    "corpus/units_train.json":   "the wider annotated set the store is selected FROM; holding "
                                 "non-shipped relations is what makes an out-of-store holdout possible",
    # --- historical measurements, kept so their numbers stay reproducible -------------------------
    "corpus/arm_20.json":        "coverage-experiment arm, a record of what was measured then",
    "corpus/arm_35.json":        "coverage-experiment arm",
    "corpus/arm_53.json":        "coverage-experiment arm",
    "corpus/arm_86.json":        "coverage-experiment arm",
    "corpus/at_risk.json":       "review list of records flagged at risk, pre-cleaning",
    "corpus/d1_held.json":       "refusal-design intermediate",
    "corpus/d1_twins.json":      "refusal-design intermediate",
    "corpus/req_judgment.json":  "review of the `req` field, pre-cleaning",
    "corpus/unconstrained_all.json": "review list of unconstrained records, pre-cleaning",
    "corpus/clean_surface_part1.json": "superseded partial of clean_surface.json",
    "corpus/unconstrained_b1.json":  "review list, pre-cleaning",
    "corpus/units_bad_records.json": "the REJECTS list -- it is supposed to hold records the store lacks",
    "corpus/units_batch1.json":      "mining batch 1; the project log records its readers as superseded by retrain",
    "corpus/units_mine_to_fix.json": "mining worklist, pre-cleaning",
    "corpus/units_review.json":      "review worklist, pre-cleaning",
    "tools/eval/_part1.json":        "partial of items.json, superseded",
    "tools/eval/_part2.json":        "partial of items.json, superseded",
}

# NOT declared expected, and NOT silently failed: live consumers whose staleness has real
# consequences and has not been assessed. Printed prominently every run so the question stays open
# instead of being closed by an EXPECTED entry written to make a gate green.
REVIEW = {
    "tools/eval/items.json":      "THE DEV EVAL SET. Its records are hand-authored and many are not "
                                  "in the shipped store, so the prompts it builds may be "
                                  "out-of-distribution -- the same defect that produced the "
                                  "retracted 16% on split_select. gate_items_refs re-executes its "
                                  "calls, which checks the ANSWERS and not the record shapes.",
    "tools/eval/record_gate.json": "used by the record gate; provenance not established.",
}


def store_formulas():
    d = json.loads((ROOT / "corpus/store_clean.json").read_text())
    d = d if isinstance(d, list) else d.get("records", [])
    return {r["f"] for r in d if r.get("f")}


def walk(o, out):
    if isinstance(o, dict):
        for k, v in o.items():
            if k in ("f", "formula", "record") and isinstance(v, str):
                cand = _rf_formula(v)
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
        if rel in REVIEW:
            rows.append((rel, len(found), missing, "__REVIEW__")); continue
        rows.append((rel, len(found), missing, why))
        if why is None: unexpected += 1
    print(f"  tracked JSON naming formulas: {len(rows)} reference records absent from the clean store\n")
    review = [r for r in rows if r[3] == "__REVIEW__"]
    rows = [r for r in rows if r[3] != "__REVIEW__"]
    for rel, n, missing, why in sorted(rows, key=lambda r: (r[3] is not None, r[0])):
        tag = "EXPECTED" if why else "UNEXPLAINED"
        print(f"  {tag:11} {rel}   {len(missing)} of {n} not in store")
        if why: print(f"              reason: {why}")
        else:   print(f"              {', '.join(missing[:4])}{' ...' if len(missing) > 4 else ''}")
    if review:
        print("\n  OPEN -- live consumers holding non-store records, not assessed:")
        for rel, n, missing, _ in review:
            print(f"    {rel}   {len(missing)} of {n} not in store")
            print(f"      {REVIEW[rel]}")
    if unexpected:
        print(f"\n  FAIL: {unexpected} artefact(s) keep records the cleaning removed and are not")
        print("  declared as raw inputs or held-out sets. Either re-derive them from the cleaned")
        print("  store, or add an EXPECTED entry saying why they legitimately differ.")
        return 1
    print("\n  PASS: every artefact naming a non-store record is a declared raw input or held-out set")
    return 0


if __name__ == "__main__":
    sys.exit(main())
