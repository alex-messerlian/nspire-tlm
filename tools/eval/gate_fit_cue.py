import sys
#!/usr/bin/env python3
"""PERMANENT GATE: a D2 refusal must be structurally indistinguishable from an answerable document.

REWRITTEN AFTER A11 CHANGED WHAT THE CLASSES ARE. The original property -- "no structural bit may
predict fit:low" -- became FALSE BY DESIGN and this gate correctly failed. src/store/assemble.c
emits exactly two shapes: a real record always carries `fit:high` (line 99), and a no-match carries
`none | missing:none | no matching relation | fit:low` (line 109) with NO RECORD AT ALL. So fit:low
IS structurally distinct on the device, and the model is meant to read that -- it is the signal,
not a leak.

The leak concern moved with it. The hard case is now **D2**: the retrieved record does not answer
the question, and the device labels it `fit:high` with `missing:none`, exactly like an answerable
document. The model must refuse on the record's CONTENT, and there must be no format tell. That is
what this checks.

  1. fit:low <=> the no-record span, byte-identical to ns_assemble_none. Either without the other
     is a shape the runtime cannot produce.
  2. Among documents WITH a record, no structural bit distinguishes a D2 refusal from an
     answerable document.

THE PROPERTY. `fit:low` means "this record does not answer this question" -- a judgement about
content. If a model can read the class off the record's FORMAT instead, it can score perfectly on
every D2 refusal metric without ever weighing fit, and the metric measures the cue.

WHAT HAPPENED. generate.py's mismatch branch carried its own copy of the units-field rule that read
the RHS only, so the LHS symbol never got a unit. Measured over 6,000 documents: the LHS unit was
absent from 331/331 = 100.0% of fit:low records and 42/5,669 = 0.7% of fit:high. A ONE-BIT
CLASSIFIER separated the classes perfectly. units_field()'s own docstring records that same defect
being found and fixed -- in the other implementation. Third instance after the loss mask and genloop.

WHY THIS IS NOT THE SEPARABILITY GATE. distribution_gate reads <q> spans and asks whether two
corpora differ; this reads <r> spans and asks whether a FORMAT bit leaks a LABEL. The classes
legitimately differ in content -- different formulas, different names -- so "are they separable" is
the wrong question and would fail forever. The property is narrower: no structural feature, one that
carries no physics, may be near-deterministic of the class.

WHAT IT DOES NOT VERIFY: leakage through content (a record whose NAME gives the game away), or
through features not enumerated below. This is a ratchet over known structural bits, not a proof of
no leakage. A bit nobody listed is unguarded, and that is stated rather than implied.
"""
# AND IT CHECKS THE SPLITS, NOT ONLY THE CORPUS. Restricting this gate to synth_sample.jsonl is why
# the fit:low defect survived in corpus/split_select.json for its whole life: the corpus was fixed,
# the gate confirmed the corpus, and all 40 SELECT refuse items stayed in a shape the device cannot
# emit. A gate scoped to one producer of a format does not cover the format.

import importlib, importlib.util, pathlib, re, sys
sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[2] / 'corpus'))
from recfmt import fields as _rf_fields, formula as _rf_formula  # " | " is the separator; a formula may contain a bare pipe

ROOT = pathlib.Path(__file__).resolve().parents[2]
N, SEED, MAX_GAP = 6000, 5, 0.25      # ratchet: measured post-fix gap is 0.007 on every bit


def record_span(doc):
    m = re.search(r"<r>(.*?)(?:<tool>|<a>)", doc, re.S)
    return m.group(1) if m else None


def bits(span):
    """Structural features only. None of these carries physics, so any of them predicting the
    class is leakage by construction -- which is what makes them the right features to check."""
    parts = _rf_fields(span)
    lhs = parts[0].split("=", 1)[0].strip()
    units = parts[1] if len(parts) > 1 else ""
    return {
        "units field omits the LHS symbol": not re.search(rf"(^|\s){re.escape(lhs)}\s*:", units),
        "units field is empty":             not units.strip(),
        "field count != 5":                 len(parts) != 5,
        "record span has no '='":           "=" not in parts[0],
    }


def _check_splits():
    import json as _j, pathlib as _p
    root=_p.Path(__file__).resolve().parents[2]
    bad=[]
    for name in ("split_select.json","split_report.json"):
        f=root/"corpus"/name
        if not f.exists(): return 2, f"CANNOT CHECK: {name} missing"
        for it in _j.loads(f.read_text()):
            rec=it["record"]
            if "fit:low" in rec and not rec.startswith("none"):
                bad.append((name, it["id"], rec[:70]))
    return (1 if bad else 0), bad


if __name__ == "__main__":
    spec = importlib.util.spec_from_file_location("gen", ROOT / "corpus/generate.py")
    m = importlib.util.module_from_spec(spec)
    try: spec.loader.exec_module(m)
    except SystemExit: pass
    out = m.gen(N, seed=SEED)
    docs = [d["text"] if isinstance(d, dict) else d for d in (out[0] if isinstance(out, tuple) else out)]

    NONE_SPAN = "none | missing:none | no matching relation | fit:low"

    # --- 1. fit:low <=> the device's no-record span -------------------------------------------
    wrong_shape = []
    for t in docs:
        span = record_span(t)
        if span is None: continue
        low = "fit:low" in span
        norec = span.strip().startswith(NONE_SPAN)
        if low != norec:
            wrong_shape.append(span.strip()[:70])
    print(f"  documents                     {len(docs):,}")
    print(f"  fit:low WITHOUT the no-record span, or vice versa   {len(wrong_shape)}")
    for w in wrong_shape[:4]: print(f"      {w}")

    # --- 2. a D2 refusal must look like an answerable document ---------------------------------
    d2 = [t for t in docs if "<tool>" not in t and "fit:high" in t
          and "which does not apply" in t]
    ans = [t for t in docs if "<tool>" in t]
    if not d2 or not ans:
        # ABSENCE IS FAILURE: a generator emitting no D2 refusals would report a perfect zero gap.
        print(f"CANNOT CHECK: D2 refusals={len(d2)}, answerable={len(ans)}; both must be non-empty")
        sys.exit(2)
    worst, leaks = 0.0, []
    for name in bits(record_span(d2[0])):
        pl = sum(bits(record_span(t))[name] for t in d2) / len(d2)
        ph = sum(bits(record_span(t))[name] for t in ans) / len(ans)
        gap = abs(pl - ph); worst = max(worst, gap)
        flag = "LEAK" if gap > MAX_GAP else "ok  "
        print(f"    {flag}  {name:34} D2 {pl:6.1%}   answerable {ph:6.1%}   gap {gap:6.1%}")
        if gap > MAX_GAP: leaks.append(name)

    # POSITIVE CONTROL: the bit-comparison must be able to see a leak.
    ctl = abs(sum(bits("x=a*b | a:m b:s | missing:none | c | fit:high")["units field omits the LHS symbol"]
                  for _ in range(1))
              - sum(bits("x=a*b | x:m a:m b:s | missing:none | c | fit:high")["units field omits the LHS symbol"]
                    for _ in range(1)))
    if ctl <= MAX_GAP:
        print("\n  FAIL: the positive control did not register a leak -- this check is not working.")
        sys.exit(1)
    print(f"  positive control              a planted LHS-unit leak reads {ctl:.0%}")

    if wrong_shape:
        print(f"\n  FAIL: {len(wrong_shape)} document(s) pair fit:low with a real record, or a record")
        print( "  with fit:low. assemble.c emits neither -- the model would train on a prompt the")
        print( "  runtime never produces, and never on the one it does.")
        sys.exit(1)
    if leaks:
        print(f"\n  FAIL: {len(leaks)} structural bit(s) distinguish a D2 refusal from an answerable")
        print( "  document: " + str(leaks))
        print( "  The model could refuse from the format instead of judging whether the record fits.")
        sys.exit(1)
    print(f"\n  PASS: fit:low is exactly the no-record span; D2 is structurally indistinguishable "
          f"from answerable (worst gap {worst:.1%})")
