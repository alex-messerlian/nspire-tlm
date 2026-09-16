#!/usr/bin/env python3
"""The committed key-adjacency table must equal the SDK's own scan matrix.

WHY A GATE AND NOT A COMMENT. corpus/keypad.py carries the adjacency because vendor/Ndless is a
nested clone whose headers this repo does not track, so nothing can be parsed at import time in a
fresh checkout. That makes TWO files carrying one fact, which is how three separate corrections in
this repo failed to propagate: store_clean.json against units_train.json, store_clean.json against
build/store.tns, and store_clean.json against units_holdout.json. A fix to one is a hypothesis
until something checks the other.

WHAT IT IS FOR. The table was qwerty and the device is alphabetical. Both tools that model a
student's typing read it -- gate_fuzzy.py, which produced the A56 headline, and
corpus/asks_explain.py, which roughens every training question. A wrong table is not visible in
either one's output: a substitution from the wrong neighbour set is still a plausible typo.

WHEN THE SDK IS ABSENT this prints SKIP and says so. "Cannot check" and "checked and clean" must
never share an exit status.
"""
import pathlib, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "corpus"))
import keypad


def main():
    d = keypad.derive()
    if d is None:
        print("  SKIP gate_keypad: vendor/Ndless SDK absent, the committed table is UNCHECKED")
        print(f"        (looked for {keypad.SDK_KEYS_H})")
        return 0
    if d == keypad.NEIGHBOURS:
        print(f"  PASS gate_keypad: committed adjacency matches the SDK scan matrix "
              f"for all {len(d)} letters")
        return 0
    bad = {k: (keypad.NEIGHBOURS.get(k), d.get(k))
           for k in set(keypad.NEIGHBOURS) | set(d) if keypad.NEIGHBOURS.get(k) != d.get(k)}
    print(f"  FAIL gate_keypad: {len(bad)} letter(s) disagree (committed, derived):")
    for k, v in sorted(bad.items()):
        print(f"      {k}: {v[0]!r} != {v[1]!r}")
    print("  The SDK is the authority. Re-derive with: python3 corpus/keypad.py")
    return 1


if __name__ == "__main__":
    sys.exit(main())
