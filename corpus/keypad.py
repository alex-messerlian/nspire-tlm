#!/usr/bin/env python3
"""Physical key adjacency on the TI-Nspire CX II keypad.

WHY THIS IS NOT A DETAIL. Two places in this repo model a student's typing mistakes:
`tools/eval/gate_fuzzy.py`, which produced the A56 headline ("retrieval holds at 79.3% with every
word misspelled"), and `corpus/asks_explain.py`, which roughens the training questions. Both
substituted a letter drawn from a table whose own comment said "adjacent keys on a qwerty-ish
layout".

The Nspire keypad is not qwerty-ish. It is alphabetical, four columns wide. Derived from the
hardware scan matrix in ndless-sdk/include/keys.h -- each key's (row, column) is its physical
position, so adjacency is a fact to read off, not a layout to recall:

        .  z  y  x
        w  v  u  t
        s  r  q  p
        o  n  m  l
        k  j  i  h
        g  f  e  d
        c  b  a  .

So on this device `a` neighbours `b` and `e`. Under the qwerty table it neighboured `s` and `q`,
which are three and four rows away. Every "adjacent key" substitution either tool has ever made was
drawn from the wrong distribution.

The finding this does NOT overturn: A56's mechanism is bounded Levenshtein, which counts edits and
does not care WHICH letter was substituted, so the 79.3% figure is not invalidated by the wrong
neighbour table. It is re-measured with the right one anyway, because a number whose input model
was wrong is a number nobody has checked.

WHY THE TABLE IS COMMITTED AND ALSO DERIVED. vendor/Ndless is a nested clone and its headers are
not tracked here, so parsing keys.h at import would break any checkout without the SDK. The table
below is therefore the shipped fact and `tools/eval/gate_keypad.py` re-derives it from the header
whenever the SDK is present. Two files carrying one fact is how three corrections in this repo
failed to propagate; the gate is what makes the second one a check rather than a copy.
"""
import pathlib
import re

# Orthogonal neighbours on the scan matrix. Derived, not typed: see derive() and gate_keypad.py.
NEIGHBOURS = {
    "a": "be",   "b": "acf",  "c": "bg",   "d": "eh",   "e": "adfi",
    "f": "begj", "g": "cfk",  "h": "dil",  "i": "ehjm", "j": "fikn",
    "k": "gjo",  "l": "hmp",  "m": "ilnq", "n": "jmor", "o": "kns",
    "p": "lqt",  "q": "mpru", "r": "nqsv", "s": "orw",  "t": "pux",
    "u": "qtvy", "v": "ruwz", "w": "sv",   "x": "ty",   "y": "uxz",
    "z": "vy",
}

SDK_KEYS_H = (pathlib.Path(__file__).resolve().parents[1]
              / "vendor/Ndless/ndless-sdk/include/keys.h")


def derive(header=SDK_KEYS_H):
    """Re-derive the table from the SDK's scan matrix. Returns None when the SDK is absent."""
    p = pathlib.Path(header)
    if not p.exists():
        return None
    txt = p.read_text(errors="ignore")
    pos = {}
    for m in re.finditer(
            r"KEY_NSPIRE_([A-Z])\s*=\s*KEY(?:TPAD)?_\((0x[0-9A-Fa-f]+),\s*(0x[0-9A-Fa-f]+)", txt):
        pos[m.group(1).lower()] = (int(m.group(2), 16), int(m.group(3), 16))
    if len(pos) != 26:
        return None
    rows = sorted({r for r, _ in pos.values()})
    cols = sorted({c for _, c in pos.values()})
    grid = {(rows.index(r), cols.index(c)): L for L, (r, c) in pos.items()}
    out = {}
    for (ri, ci), L in grid.items():
        n = "".join(sorted(grid[(ri + dr, ci + dc)]
                           for dr, dc in ((0, 1), (0, -1), (1, 0), (-1, 0))
                           if (ri + dr, ci + dc) in grid))
        out[L] = n
    return dict(sorted(out.items()))


def grid_art(header=SDK_KEYS_H):
    """The layout as a picture, for a human reading a failure."""
    p = pathlib.Path(header)
    if not p.exists():
        return "(SDK absent)"
    txt = p.read_text(errors="ignore")
    pos = {}
    for m in re.finditer(
            r"KEY_NSPIRE_([A-Z])\s*=\s*KEY(?:TPAD)?_\((0x[0-9A-Fa-f]+),\s*(0x[0-9A-Fa-f]+)", txt):
        pos[m.group(1).lower()] = (int(m.group(2), 16), int(m.group(3), 16))
    rows = sorted({r for r, _ in pos.values()})
    cols = sorted({c for _, c in pos.values()})
    grid = {(rows.index(r), cols.index(c)): L for L, (r, c) in pos.items()}
    return "\n".join(" ".join(grid.get((ri, ci), ".") for ci in range(len(cols)))
                     for ri in range(len(rows)))


assert all(len(k) == 1 and k.isalpha() for k in NEIGHBOURS), "keys are single letters"
assert len(NEIGHBOURS) == 26, f"{len(NEIGHBOURS)} letters, expected 26"
# adjacency is symmetric, and an asymmetry means a hand edit went in one direction only
for _a, _ns in NEIGHBOURS.items():
    for _b in _ns:
        assert _a in NEIGHBOURS[_b], f"{_a}-{_b} adjacency is not symmetric"


if __name__ == "__main__":
    d = derive()
    print("PHYSICAL LAYOUT (from the SDK scan matrix):")
    print(grid_art())
    if d is None:
        print("\nSDK absent: cannot re-derive. The committed table stands unchecked.")
    elif d == NEIGHBOURS:
        print(f"\ncommitted table matches the SDK header for all {len(d)} letters")
    else:
        diff = {k: (NEIGHBOURS.get(k), d.get(k)) for k in set(NEIGHBOURS) | set(d)
                if NEIGHBOURS.get(k) != d.get(k)}
        print(f"\nMISMATCH on {len(diff)}: {diff}")
