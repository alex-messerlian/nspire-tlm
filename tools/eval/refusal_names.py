#!/usr/bin/env python3
"""When the model declines for a missing value, does it name the value that is actually missing?

    python3 tools/eval/refusal_names.py results/arms_int8_<engine>_ship.json [...]

Declining is scored by refusal_strict, which asks only whether the output declines without a tool
call. The device transcript showed a decline that named the wrong variable ("d_i is not given" under
a record span saying missing:d_o), which that grader passes. This reads the SAVED generations of the
two decline arms (d1: one needed value withheld; d1_zero: no values) and asks the narrower question.

DEFINITION. A decline NAMES CORRECTLY iff it has the form "... X is not given" (or "X and Y are not
given") and every variable so named appears in the prompt's own `missing:` field -- the field the
runtime wrote, so the reference is what the model was shown. A decline of any other form counts as
not naming correctly; that is counted, not dropped. The denominator is the declines, not the items.

Recorded first in A151 from an ad hoc run (results/refusal_names_missing.txt: group 32 d1 104/120,
group 88 d1 98/119). This is that measurement as a script; it reproduces both before it is used.
"""
import json, re, sys

NAMED = re.compile(r"([A-Za-z_][A-Za-z0-9_]*(?:\s*(?:,|and)\s*[A-Za-z_][A-Za-z0-9_]*)*)\s+(?:is|are) not given")
MISSING = re.compile(r"\|\s*missing:([^|]*)\|")


def named_ok(prompt, gen):
    m = MISSING.search(prompt)
    missing = {v.strip() for v in re.split(r"[ ,]+", m.group(1)) if v.strip()} if m else set()
    n = NAMED.search(gen or "")
    if not n:
        return False
    names = {v for v in re.split(r"\s*(?:,|and)\s*", n.group(1)) if v}
    return bool(names) and names <= missing


def main(paths):
    for path in paths:
        r = json.load(open(path))
        for arm in ("d1", "d1_zero"):
            rows = r["arms"][arm]["rows"]
            declined = [x for x in rows if x["ok"]]
            good = [x for x in declined if named_ok(x["prompt"], x["gen"])]
            bad = [x["gen"].strip()[:70] for x in declined if not named_ok(x["prompt"], x["gen"])]
            print(f"{path.split('/')[-1]:28s} {arm:8s} declined {len(declined)}/{len(rows)}; "
                  f"names only missing variables {len(good)}/{len(declined)} = "
                  f"{100 * len(good) / max(len(declined), 1):.1f}%   e.g. {bad[:1]}")


if __name__ == "__main__":
    main(sys.argv[1:])
