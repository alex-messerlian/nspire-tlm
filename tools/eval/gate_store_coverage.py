#!/usr/bin/env python3
"""PERMANENT GATE: R_train superseteq R_store -- every retrievable record must be a trained one.

THE RULE. The device ships corpus/store_clean.json and the picker can return ANY of it. A record
the model has never seen produces a well-formed document with the wrong structure: measured
**41.0% correct where the corpus contains the relation, 12.2% where it does not**, three seeds.
That is the only property in this project with a measured effect on whether an answer is right.

**Shipping a relation you did not train on is shipping a confident wrong answer with a retrieval
path to it.** Nothing else in the suite notices -- the document is well-formed, the call executes,
the result matches, provenance is clean and the shape checks.

WHAT IT WAS. 25 of 166 store records were untrained, because generate.py iterated the ANNOTATED set
(units_train) and those 25 were not in it. All 25 already carried complete units and a name in the
store; the iteration simply never reached them. Two more were rejected by a name filter whose bare
`Theorem` marker caught "Work-energy theorem" and "impulse-momentum theorem".

WHAT THIS DOES NOT VERIFY: that the generated documents for a covered relation are CORRECT -- that
is gate_ask_quantity, gate_no_orphan_values and the rest -- or that coverage of relations OUTSIDE
the store matters. It verifies the containment, which is the rule.

DIRECTION MATTERS. R_train may exceed R_store (the corpus may teach relations the device cannot
retrieve; harmless). The failure is a store record with no training, and only that.
"""
import importlib.util, io, contextlib, json, pathlib, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]

if __name__ == "__main__":
    spec = importlib.util.spec_from_file_location("gen", ROOT / "corpus/generate.py")
    m = importlib.util.module_from_spec(spec)
    try:
        with contextlib.redirect_stdout(io.StringIO()):
            spec.loader.exec_module(m)
    except SystemExit:
        pass
    store = json.load(open(ROOT / "corpus/store_clean.json"))
    if not store or not getattr(m, "recs", None):
        # ABSENCE IS FAILURE: an empty store trivially satisfies containment, and an empty
        # generator trivially violates it. Neither is "checked and clean".
        print(f"CANNOT CHECK: store={len(store)} generator={len(getattr(m, 'recs', []))}")
        sys.exit(2)

    trained = {r["f"] for r in m.recs}
    untrained = [r for r in store if r["f"] not in trained]
    print(f"  store records (retrievable)  {len(store)}")
    print(f"  relations the corpus teaches {len(trained)}")
    print(f"  SHIPPED BUT UNTRAINED        {len(untrained)}")
    for r in untrained[:12]:
        print(f"      {r['f'][:38]:40} {(r.get('name') or '')[:40]}")
    if untrained:
        print(f"\n  FAIL: {len(untrained)} record(s) the device can retrieve are never trained on.")
        print( "  Measured cost: 12.2% correct on an untrained relation against 41.0% on a trained")
        print( "  one. Either teach them or delete them from the store -- the picker cannot be")
        print( "  asked to avoid them.")
        sys.exit(1)
    print("\n  PASS: R_train superseteq R_store")
