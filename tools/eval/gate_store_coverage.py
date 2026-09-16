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

def read_packed(path):
    """The device's own view of the store: the R lines of build/store.tns.

    Returns [{f, name, kind}], where kind is "knowledge" when the record's single variable is
    declared `text` -- which is how a K-TEXT definition marks itself -- and "compute" otherwise.
    """
    if not path.exists():
        return None
    recs, lines = [], path.read_text().split("\n")
    i = 0
    while i < len(lines):
        fl = lines[i].split("\t")
        if fl[0] == "R" and len(fl) >= 7:
            nv = int(fl[6]) if fl[6].isdigit() else 0
            units = []
            for j in range(1, nv + 1):
                if i + j < len(lines):
                    v = lines[i + j].split("\t")
                    if v[0] == "V" and len(v) >= 3:
                        units.append(v[2])
            recs.append({"f": fl[3], "name": fl[4],
                         "kind": "knowledge" if units == ["text"] else "compute"})
            i += nv + 1
            continue
        i += 1
    return recs


if __name__ == "__main__":
    spec = importlib.util.spec_from_file_location("gen", ROOT / "corpus/generate.py")
    m = importlib.util.module_from_spec(spec)
    try:
        with contextlib.redirect_stdout(io.StringIO()):
            spec.loader.exec_module(m)
    except SystemExit:
        pass
    # THE PACKED STORE, NOT THE JSON. build/store.tns is what the device loads and what the picker
    # can return; store_clean.json is only one of its inputs. Reading the JSON made this gate
    # STRUCTURALLY BLIND to the knowledge tier: 1,443 K-TEXT records were packed into the shipped
    # store and it reported "SHIPPED BUT UNTRAINED 0", because those records exist in no JSON.
    #
    # Same class as the three propagation failures already recorded, mirrored: there a fix to the
    # JSON never reached the packed store, here a record in the packed store was invisible to the
    # check. Ask the ARTEFACT THE DEVICE READS, every time.
    store = read_packed(ROOT / "build/store.tns")
    if store is None:
        print("CANNOT CHECK: build/store.tns absent -- run tools/store_pack.py. Not a pass.")
        sys.exit(2)
    if not store or not getattr(m, "recs", None):
        # ABSENCE IS FAILURE: an empty store trivially satisfies containment, and an empty
        # generator trivially violates it. Neither is "checked and clean".
        print(f"CANNOT CHECK: store={len(store)} generator={len(getattr(m, 'recs', []))}")
        sys.exit(2)

    trained = {r["f"] for r in m.recs}
    # KNOWLEDGE TERMS ARE ASKED OF THE GENERATOR, exactly like relations, and NOT of
    # corpus/knowledge/definitions_train.json.
    #
    # That file is the generator's INPUT: a term in it is SLATED for training, not trained. Reading
    # it here reported "SHIPPED BUT UNTRAINED 0" for 1,443 records no corpus mentions, which is the
    # false-closure this gate exists to prevent -- an open defect is on a list somebody re-reads,
    # a defect wrongly marked closed is protected from the next audit.
    #
    # generate.py publishes `kterms` once it emits knowledge documents. Until then the set is empty
    # and every packed knowledge record is correctly reported untrained, which is what makes
    # "pack it before you teach it" fail rather than pass.
    ktrained = set(getattr(m, "kterms", ()) or ())
    def is_untrained(r):
        return (r["f"] not in ktrained) if r["kind"] == "knowledge" else (r["f"] not in trained)
    untrained = [r for r in store if is_untrained(r)]
    nk = sum(1 for r in store if r["kind"] == "knowledge")
    print(f"  store records (retrievable)  {len(store)}  "
          f"({len(store)-nk} compute, {nk} knowledge)")
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
