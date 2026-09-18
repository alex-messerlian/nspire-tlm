"""Host: pack corpus/store_clean.json into the flat .store format the device loader reads.

No JSON on device. Format is line-based and parseable with a hand-written scanner:

    NSTORE1
    <count>
    R<TAB>rid<TAB>lhs<TAB>formula<TAB>name<TAB>req<TAB>nvars
    V<TAB>var<TAB>unit<TAB>const-or-empty          (nvars of these)
    ...
    END<TAB><count>

The trailing END line carries the count again, so a TRUNCATED FILE IS DETECTABLE. A loader that
stops early would otherwise return a short store and read as success -- the silent-empty failure
this format exists to prevent."""
import json, os, sys, pathlib, re

VAR = re.compile(r"(?<![A-Za-z0-9_])([A-Za-z][A-Za-z0-9_]*)(?![A-Za-z0-9_(])")
RESV = {"pi","e","sin","cos","tan","ln","log","sqrt","exp","asin"}


def _assert_decisions_hold(store):
    """Decisions that would otherwise be held by documentation alone. See docs/BASE_RESOLUTION.md,
    docs/E_SPEC.md, docs/RESULT_STORE_CLEANING.md. Each raises where a caller trips on it."""
    import sys
    sys.path.insert(0, "tools/eval")
    import lhs_gate
    v = lhs_gate.violations(store)
    assert not v, f"LHS gate: {len(v)} record(s) whose LHS is not a single identifier -- " \
                  f"K*E parses as K times E. First: {v[0][0]['f']!r}"
    missing = [r for r in store if not r.get("verified")]
    assert not missing, f"{len(missing)} record(s) lack a `verified` status -- the " \
                        f"unverifiable-no-source marker must stay VISIBLE in the shipped store, " \
                        f"not only in a doc. See docs/RESULT_STORE_CLEANING.md."
    # E deletes retrieval from the device. If a retrieval term ever ships, the scorer came back.
    leaked = [r for r in store if "terms" in r]
    assert not leaked, f"{len(leaked)} record(s) carry retrieval `terms`. Under E nothing is " \
                       f"retrieved at inference (measured -14.4 pp, anti-correlated). Shipping " \
                       f"terms means the deleted scorer has returned. See docs/E_SPEC.md."
    # The clean surface is load-bearing for every retrieval claim. Deleting a record silently
    # orphaned 9 of its labels -- the ground truth pointed at records that no longer existed and
    # nothing reported it. Same class as the stale artifact.
    import os.path
    if os.path.exists("corpus/clean_surface.json"):
        surf = json.load(open("corpus/clean_surface.json"))
        have = {r["f"] for r in store}
        orph = [x for x in surf["items"] if x.get("formula") and x["formula"] not in have]
        assert not orph, (f"{len(orph)} clean-surface label(s) point at records not in the store, "
                          f"first {orph[0]['formula']!r} -- repair the surface or restore the record")
    # ASCII-ONLY. The device tokenizer's GPT-2 regex is implemented with isalpha/isdigit, which
    # is correct ONLY while no byte exceeds 127. A single non-ASCII character in a record would
    # mis-split silently on device and produce different tokens than the host. See the tokenizer
    # port notes in docs/DEVICE_PLAN_E.md.
    for r in store:
        for k in ("f", "name", "req"):
            v = r.get(k)
            if isinstance(v, str) and any(ord(c) > 127 for c in v):
                bad = [c for c in v if ord(c) > 127][:3]
                raise AssertionError(f"record {r.get('rid')} field {k!r} is not ASCII ({bad}) -- "
                                     f"the device tokenizer assumes ASCII; normalise at pack time")
    ids = [r.get("rid") for r in store]
    assert all(ids) and len(set(ids)) == len(ids), \
        "records must carry unique stable rids -- position-based identity silently skipped " \
        "records during the hand-check batches."

def pack(store):
    _assert_decisions_hold(store)
    out = ["NSTORE1", str(len(store))]
    for r in store:
        f = r["f"]
        lhs, rhs = f.split("=", 1)
        lhs = lhs.strip()
        # LHS FIRST, then RHS. The device needs the solved-for variable's unit to label the
        # answer AND to key the picker family -- packing only RHS variables left every record
        # unmapped, caught by test_picker's positive control rather than by any gate.
        vs = [lhs] + [v for v in dict.fromkeys(VAR.findall(rhs)) if v not in RESV and v != lhs]
        units = r.get("units") or {}
        cvals = r.get("cval") or {}
        # "REVIEWED, NO RESTRICTION" MUST BE DISTINGUISHABLE FROM "NOBODY LOOKED".
        #
        # This was r.get("req", "standard conditions"), and it collapsed three different states
        # into one byte string:
        #   key absent          -> "standard conditions"   (nobody has reviewed this record)
        #   "req": false        -> "standard conditions"   (reviewed; the relation is general)
        #   "req": null         -> None -> TypeError on the delimiter assert below, which is how
        #                          a 54-agent authoring pass discovered it
        # A condition pass produced 15 considered nulls and every one of them reached the packer
        # as bytes identical to a record nobody had opened, so the decision survived only in review
        # notes. The project log: a decision held by documentation alone will lapse.
        #
        # `false` is now the reviewed-general marker. It still EMITS the default -- the device
        # prompt is unchanged, which is the point: this distinction is for the repo, not the model.
        # An explicit null is a hard error naming the fix rather than a TypeError three lines down.
        _req = r.get("req", None)
        if _req is None and "req" in r:
            raise AssertionError(
                f"record {r.get('rid')!r} has \"req\": null. Use false for 'reviewed, no physical "
                f"restriction' or omit the key for 'not yet reviewed'; null is neither and packs "
                f"as a TypeError.")
        if _req is False:
            _req = "standard conditions"
        elif _req is None:
            _req = "standard conditions"
        elif not isinstance(_req, str):
            raise AssertionError(f"record {r.get('rid')!r} req must be a string or false, got {_req!r}")
        for field in (r["rid"], lhs, f, r["name"], _req):
            assert "\t" not in field and "\n" not in field, f"field contains a delimiter: {field!r}"
        out.append("\t".join(["R", r["rid"], lhs, f, r["name"], _req, str(len(vs))]))
        for v in vs:
            out.append("\t".join(["V", v, units.get(v, "?"), str(cvals.get(v, ""))]))
    out.append(f"END\t{len(store)}")
    return "\n".join(out) + "\n"


# ---------------------------------------------------------------------------------------------
# THE KNOWLEDGE TIER (K-TEXT). A definition packed as an ordinary store record, so NOT ONE LINE OF
# DEVICE C CHANGES. ns_assemble already emits the right five fields for it:
#
#     <q>what is aberration?</q><r>aberration | term:text | missing:none | <meaning> | fit:high
#
# HOW THE FIELDS FALL OUT, each from a line of src/store/assemble.c that is unchanged:
#   formula  the term. Field 0, which is what the reader sees first.
#   var[0] == lhs  so assemble.c:114's missing loop `continue`s on its only variable and `miss`
#            stays "none" with no invented value and no new branch.
#   unit[0]  "text", a POSITIVE marker that this record defines rather than computes. An empty
#            units field would be a shape with 0 documents in 239,853 and byte-indistinguishable
#            from a compute record whose V lines were dropped.
#   req      the meaning. Field 3 is the only field that already carries free prose.
#   cval[0]  empty, so assemble.c:90's constant-inlining loop writes nothing and no " Given " is
#            emitted for a question that supplies nothing.
#
# WHY var[0] IS "term" AND NOT THE TERM ITSELF. assemble.c only requires var[0] == lhs; it does
# NOT require lhs == formula. Using the term for both would put a SPACE inside a variable name on
# 1,086 of 1,443 records (75.3%) -- "absolute pressure:text" -- and the units field is
# space-delimited `var:unit` on all 232,385 five-field documents. tools/eval/grade.py:154 parses it
# with `parts[1].split()`, so a spaced name silently returns "no unit required": the right answer
# for a definition, reached by a parse failure, which is the dim_gate defect ("cannot check" must
# not share a value with "checked and nothing needed"). A fixed `term` keeps the grammar intact and
# costs nothing, because the term is still field 0 and still the record's name.
#
# A SECOND PROPERTY FALLS OUT FREE: ask_pick's supplied-variable term compares typed variables
# against rec.var[k] and skips the lhs, so a question carrying givens can never score a knowledge
# record on its variable. A definition takes no givens, and now cannot be retrieved by one.
KNOWLEDGE_VAR = "term"
KNOWLEDGE_UNIT = "text"


def knowledge_lines(defs):
    """Pack term/meaning pairs as K-TEXT records. Returns (lines, n)."""
    import hashlib
    out, seen = [], set()
    for d in defs:
        term, mean = d["term"].strip(), d["meaning"].strip()
        if not term or not mean:
            continue
        key = term.lower()
        if key in seen:
            continue
        seen.add(key)
        rid = "k" + hashlib.sha256(("ktext/v1:" + key).encode()).hexdigest()[:7]
        for field in (rid, term, mean):
            assert "\t" not in field and "\n" not in field, f"delimiter in {field!r}"
            assert all(ord(c) < 128 for c in field), f"non-ASCII in {field!r}"
        out.append("\t".join(["R", rid, KNOWLEDGE_VAR, term, term, mean, "1"]))
        out.append("\t".join(["V", KNOWLEDGE_VAR, KNOWLEDGE_UNIT, ""]))
    return out, len(seen)


if __name__ == "__main__":
    store = json.load(open("corpus/store_clean.json"))
    txt = pack(store)
    # THE KNOWLEDGE TIER, appended. The held-out 150 are NOT here: a retrievable record the model
    # was never trained on is what gate_store_coverage exists to fail on, measured at 41.0% against
    # 12.2%. definitions_train.json is the A58 carve and is the only file read.
    #
    # OPT-IN, AND OFF BY DEFAULT, until corpus/generate.py actually teaches these terms.
    # gate_store_coverage's rule is R_train superseteq R_store: a record the picker can return and
    # the model has never seen is measured at 12.2% correct against 41.0% for a trained one. Packing
    # 1,443 definitions before the corpus mentions them puts 1,443 fabrication sites on the device.
    # Build and test the knowledge store freely with WITH_KNOWLEDGE=1; ship it when the gate passes.
    kpath = pathlib.Path("corpus/knowledge/definitions_train.json")
    nk = 0
    if kpath.exists() and os.environ.get("WITH_KNOWLEDGE"):
        klines, nk = knowledge_lines(json.load(open(kpath)))
        body = txt.rstrip("\n").split("\n")
        assert body[-1].startswith("END\t"), "pack() must end with its END line"
        total = len(store) + nk
        txt = "\n".join(body[:1] + [str(total)] + body[2:-1] + klines + [f"END\t{total}"]) + "\n"
    # STORE_OUT lets a check build a store WITHOUT touching the one the device and every other
    # gate read. gate_knowledge_bytes needs a knowledge store to diff against; repacking the
    # shipped path and repacking back would leave a window where any concurrent reader sees a
    # store nobody intended to ship.
    dest = pathlib.Path(os.environ.get("STORE_OUT", "build/store.tns"))
    tmp = pathlib.Path(str(dest) + ".tmp"); tmp.parent.mkdir(parents=True, exist_ok=True)
    tmp.write_text(txt)
    tmp.rename(dest)                       # atomic: a crash leaves no half-written store
    print(f"packed {len(store)} compute + {nk} knowledge = {len(store)+nk} records "
          f"-> {dest} ({len(txt):,} bytes)")
    print(f"  largest record line: {max(len(l) for l in txt.split(chr(10)))} chars")
