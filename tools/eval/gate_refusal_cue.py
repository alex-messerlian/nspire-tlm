#!/usr/bin/env python3
"""How much of the refusal decision can be made without reading the record?

`missing:` is filled by the runtime with the shown record's first unbound variable, so it is a
legitimate field and a model is right to use it. The hazard is that it is SUFFICIENT: if
`refuse <=> missing != none` classifies the whole corpus, the fit judgement is never required
during training, and D2's score becomes a measurement of the binding check wearing D2's name.

Measured at the time of writing: 99.80% accurate, because 99.0% of D2 documents carry an unbound
variable. The model trained on that corpus refuses 97.5% of ordinary D2 items and 0.0% of items
where the wrong record is fully bound -- see docs/RESULT_FIT_JUDGEMENT.md.

This is a RATCHET, not a pass/fail on the level. The rate cannot be driven to chance: a wrong record
usually does have an unbound variable, on the device too. What must not happen is the number
drifting up unnoticed, or the fit-judgement documents disappearing again the way D2 itself once did.
"""
import json, pathlib, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "corpus"))
from recfmt import fields, MISSING

CUE_CEILING = 99.85      # measured 99.80; a rise means fit-judgement documents are vanishing
PURE_FIT_FLOOR = 0.30    # % of documents that require reading the record; measured 0.05


def main():
    p = ROOT / "corpus/synth_sample.jsonl"
    if not p.exists():
        print("CANNOT CHECK: corpus/synth_sample.jsonl absent -- not a pass"); return 2
    n = right = pure = d2 = 0
    for line in p.open():
        o = json.loads(line)
        rec = o["text"].split("<r>", 1)[1].split("<", 1)[0]
        f = fields(rec)
        miss = next((x for x in f if x.startswith("missing:")), "missing:none")
        refuses = "<tool>" not in o["text"]
        cue = miss != "missing:none"
        n += 1
        right += (cue == refuses)
        if o.get("kind") == "D2":
            d2 += 1
            if not cue:
                pure += 1              # a wrong record that is fully bound: reading it is required
    acc = 100.0 * right / n
    pf = 100.0 * pure / n
    print(f"  documents {n:,}   D2 {d2:,}")
    print(f"  'refuse <=> missing != none' classifies {acc:.2f}% of the corpus")
    print(f"  documents that REQUIRE reading the record: {pure} ({pf:.2f}%)")
    bad = []
    if acc > CUE_CEILING:
        bad.append(f"cue accuracy {acc:.2f}% exceeds the {CUE_CEILING}% ratchet")
    if pf < PURE_FIT_FLOOR:
        bad.append(f"only {pf:.2f}% of documents require reading the record, below {PURE_FIT_FLOOR}%")
    for b in bad:
        print(f"  {b}")
    if bad:
        print("FAIL: the corpus is drifting toward a refusal decision that never reads the record.")
        return 1
    print("PASS: within the recorded ratchet. The rate is reported, never assumed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
