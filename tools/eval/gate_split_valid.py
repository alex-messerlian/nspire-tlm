import sys
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
sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[2] / 'corpus'))
from recfmt import fields as _rf_fields, formula as _rf_formula  # " | " is the separator; a formula may contain a bare pipe

ROOT = pathlib.Path(__file__).resolve().parents[2]
# A48. Surfaces that a mined record NAME can contribute to a question while naming no quantity.
# Measured before it was believed: 61 of 1,468 split items across three splits carried one.
NOT_A_QUANTITY = re.compile(r"\(from worked example\)|[a-z] speed:|calculating force:"
                            r"|measuring the refractive index", re.I)
LEIBNIZ = re.compile(r"\(d\*[A-Za-z_]")
FUSED   = re.compile(r"^[A-Z]\*[A-Z][A-Za-z0-9_]*=")
# Captures the VARIABLE and its VALUE -- the value is the property being checked.
# `[\d.]+` swallowed the SENTENCE-ENDING PERIOD -- "Given g = 9.81." captured "9.81." and every
# constant-bearing item read as a value mismatch against its own correct value. Harmless while this
# regex only asked whether a constant was present; a defect the moment the value became the check.
GIVEN   = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(-?\d+(?:\.\d+)?(?:[eE][-+]?\d+)?)")

# Whether an item's GRADE depends on a supplied constant having the right value. Declared per
# split and never inferred from the filename, and a split absent from this map FAILS the gate --
# a new arm must not be silently unscoped, which is exactly how `c = 117.8` for the speed of light
# sat in split_fit_m.json unexamined while this gate reported PASS on the two files it knew about.
# Files carrying a per-item `expect` field use that instead; this is the fallback for those that
# do not. See docs/RESULT_CANNOT_EXPLAIN.md for how the scoping came up.
USES_CONSTANT = {
    "split_select.json":     True,   # per-item `expect`; this is only the fallback
    "split_report.json":     True,
    "split_answer_0.json":   True,   # answerable: the model computes WITH the constant
    "split_answer_s.json":   True,
    "split_answer_w.json":   True,
    "split_answer_x.json":   True,
    "split_explain.json":    True,   # states the relation; must not contradict the given value
    "split_explain_ho.json": True,
    "split_fit.json":        False,  # refusal arms: correct behaviour is to refuse regardless
    "split_fit_ho.json":     False,
    "split_fit_m.json":      False,
    "split_d1.json":         False,
    "split_d1_zero.json":    False,
}


def main():
    store = json.loads((ROOT / "corpus/store_clean.json").read_text())
    store = store if isinstance(store, list) else store.get("records", [])
    hold  = json.loads((ROOT / "corpus/units_holdout.json").read_text())
    # THE PROPERTY IS THE VALUE, NOT THE PRESENCE. The defect this gate was written for was
    # `c = 2` for the speed of light -- a WRONG constant, scored against a model that correctly
    # inlined 2.998e8. It was checked as "does the question supply a constant at all", which is a
    # proxy: src/store/assemble.c walks r->cval independently of the input and writes
    # "Given g = 9.81." on every prompt for a constant-bearing record, so a question supplying the
    # RIGHT value is exactly what the device emits. Under the presence rule the A46 explain items
    # -- which carry the device's own inlined constants -- would have been reported invalid.
    #
    # No change for the existing splits: they supply no constants at all, so a rule that admits the
    # correct value and still rejects `c = 2` moves nothing there. Verified by running both.
    cval  = {}
    for r in list(store) + list(hold):
        for k, v in (r.get("cval") or {}).items():
            cval.setdefault(r.get("f"), {})[k] = v

    bad = []
    n = 0
    # EVERY split, enumerated from disk. A hardcoded pair means a new arm is silently unchecked,
    # and "cannot check" sharing an exit status with "checked and clean" is the defect this repo
    # has now found five times. A split that appears must be validated or must fail.
    skipped = []
    for name in sorted(p.name for p in (ROOT / "corpus").glob("split_*.json")):
        if name not in USES_CONSTANT:
            print(f"  UNSCOPED: {name} is not in USES_CONSTANT. A new arm must declare whether its")
            print( "            grading depends on a supplied constant. Refusing to report a pass.")
            return 2
        p = ROOT / "corpus" / name
        if not p.exists():
            print(f"  CANNOT CHECK: {name} is missing. Refusing to report a pass.")
            return 2
        for it in json.loads(p.read_text()):
            n += 1
            f = _rf_formula(it["record"])
            if LEIBNIZ.search(f):
                bad.append((name, it.get("id", "?"), f, "Leibniz artifact: `d` cancels, not a relation"))
            # A48. THE QUESTION MUST ASK FOR A QUANTITY. Applies to every split and every
            # expectation -- an item asking "What is (from worked example)?" is unanswerable
            # whatever it is labelled, so this is NOT scoped by USES_CONSTANT.
            #
            # gate_split_well_posed cannot catch this: it asserts an item names its record's
            # QUANTITY, and '(from worked example)' IS the record's name, so a junk name satisfies
            # the predicate. Checked here against the surface the question actually contains.
            m_nq = NOT_A_QUANTITY.search(it["q"])
            if m_nq:
                bad.append((name, it.get("id", "?"), f,
                            f"asks for {m_nq.group(0)!r}, which is not a quantity -- the question "
                            f"is unanswerable however it is labelled"))
            if FUSED.match(f):
                bad.append((name, it.get("id", "?"), f, "fused identifier from the MathML conversion"))
            # A WRONG CONSTANT PENALISES A MODEL ONLY WHERE THE MODEL MUST USE IT. On a refusal
            # item the correct behaviour is to refuse whatever the constant says, so the value is
            # not part of the grade -- and `c` legitimately means SPECIFIC HEAT in a question while
            # the mismatched record uses it for the speed of light. src/store/assemble.c lets the
            # student's value win over the record's constant, so the device emits exactly that.
            # Counted and printed rather than silently passed: "cannot check" and "checked and
            # clean" must not share an exit status.
            grades_on_it = (it["expect"] == "answer") if "expect" in it else USES_CONSTANT[name]
            if not grades_on_it:
                skipped.append(name)
                continue
            declared = cval.get(f, {})
            for var, val in GIVEN.findall(it["q"]):
                if var not in declared:
                    continue
                try:
                    same = abs(float(val) - float(declared[var])) <= 1e-9 * abs(float(declared[var]))
                except ValueError:
                    same = val == declared[var]
                if not same:
                    bad.append((name, it.get("id", "?"), f,
                                f"supplies {var} = {val}, but the runtime inlines "
                                f"{var} = {declared[var]} -- scoring a model on this penalises it "
                                f"for being right"))
    print(f"  split items checked: {n} across {len(USES_CONSTANT)} splits")
    if skipped:
        import collections as _c
        _b = _c.Counter(skipped)
        print(f"  constant-value check N/A on {len(skipped)} refusal items "
              f"({', '.join(f'{k.removeprefix(chr(115)+chr(112)+chr(108)+chr(105)+chr(116)+chr(95)).removesuffix(chr(46)+chr(106)+chr(115)+chr(111)+chr(110))} {v}' for k, v in sorted(_b.items()))})"
              " -- the model must refuse whatever the value says. Shape checks still applied.")
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
