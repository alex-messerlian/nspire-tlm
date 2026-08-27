#!/usr/bin/env python3
"""PERMANENT GATE: no STRUCTURAL bit of the record span may predict the refusal class.

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
import importlib, importlib.util, pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
N, SEED, MAX_GAP = 6000, 5, 0.25      # ratchet: measured post-fix gap is 0.007 on every bit


def record_span(doc):
    m = re.search(r"<r>(.*?)(?:<tool>|<a>)", doc, re.S)
    return m.group(1) if m else None


def bits(span):
    """Structural features only. None of these carries physics, so any of them predicting the
    class is leakage by construction -- which is what makes them the right features to check."""
    parts = span.split("|")
    lhs = parts[0].split("=", 1)[0].strip()
    units = parts[1] if len(parts) > 1 else ""
    return {
        "units field omits the LHS symbol": not re.search(rf"(^|\s){re.escape(lhs)}\s*:", units),
        "units field is empty":             not units.strip(),
        "field count != 5":                 len(parts) != 5,
        "record span has no '='":           "=" not in parts[0],
    }


if __name__ == "__main__":
    spec = importlib.util.spec_from_file_location("gen", ROOT / "corpus/generate.py")
    m = importlib.util.module_from_spec(spec)
    try: spec.loader.exec_module(m)
    except SystemExit: pass
    out = m.gen(N, seed=SEED)
    docs = [d["text"] if isinstance(d, dict) else d for d in (out[0] if isinstance(out, tuple) else out)]

    low = [s for s in (record_span(d) for d in docs if "fit:low" in d) if s]
    high = [s for s in (record_span(d) for d in docs if "fit:high" in d) if s]
    if not low or not high:
        # ABSENCE IS FAILURE. A generator that stopped emitting one class would otherwise report a
        # gap of zero on every bit and pass -- "cannot check" scoring as "checked and clean".
        print(f"CANNOT CHECK: fit:low n={len(low)}, fit:high n={len(high)}; both must be non-empty")
        sys.exit(2)

    print(f"  fit:low {len(low)}   fit:high {len(high)}   max allowed gap {MAX_GAP:.0%}")
    worst, bad = 0.0, []
    for name in bits(low[0]):
        pl = sum(bits(s)[name] for s in low) / len(low)
        ph = sum(bits(s)[name] for s in high) / len(high)
        gap = abs(pl - ph)
        worst = max(worst, gap)
        flag = "LEAK" if gap > MAX_GAP else "ok  "
        print(f"    {flag}  {name:34} low {pl:6.1%}   high {ph:6.1%}   gap {gap:6.1%}")
        if gap > MAX_GAP: bad.append(name)

    # POSITIVE CONTROL in-run: the check must be able to see a leak at all.
    planted_low = ["x=a*b | a:m b:s | missing:none | c | fit:low"] * 50
    planted_high = ["x=a*b | x:m a:m b:s | missing:none | c | fit:high"] * 50
    ctl = abs(sum(bits(s)["units field omits the LHS symbol"] for s in planted_low) / 50
              - sum(bits(s)["units field omits the LHS symbol"] for s in planted_high) / 50)
    if ctl <= MAX_GAP:
        print("\n  FAIL: the positive control did not register a leak -- this check is not working.")
        sys.exit(1)
    print(f"  positive control  a planted LHS-unit leak reads {ctl:.0%}, above the {MAX_GAP:.0%} bar")

    if bad:
        print(f"\n  FAIL: {len(bad)} structural bit(s) predict the refusal class: {bad}")
        print( "  A model can read the label off the format and score on every D2 refusal metric")
        print( "  without weighing fit. The metric would be measuring the cue.")
        sys.exit(1)
    print(f"\n  PASS: no structural bit predicts the refusal class (worst gap {worst:.1%})")
