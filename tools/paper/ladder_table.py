#!/usr/bin/env python3
"""The width-ladder table, printed from the device-decoder results -- no number typed by hand.

    .venv-tok/bin/python tools/paper/ladder_table.py [group]      (default 16)
"""
import json, pathlib, sys

RES = pathlib.Path(__file__).resolve().parents[2] / "results"
G = sys.argv[1] if len(sys.argv) > 1 else "16"
NAMES = {176: ("w176", "w176s2"), 352: ("w352", "w352s2")}
ROWS = [("decline: value withheld", "arms", "d1", "pct"),
        ("decline: no values", "arms", "d1_zero", "pct"),
        ("compute: values given", "correct", "answer_0", "device_ok_pct"),
        ("compute: symbol-named", "correct", "answer_s", "device_ok_pct"),
        ("compute: word-named", "correct", "answer_w", "device_ok_pct"),
        ("compute: extra value", "correct", "answer_x", "device_ok_pct"),
        ("explain (form)", "arms", "explain", "pct"),
        ("judge fit", "arms", "fit", "pct")]


def val(kind, name, arm, key):
    f = RES / f"{'arms' if kind == 'arms' else 'correct'}_int8_g{G}_{name}.json"
    return json.loads(f.read_text())["arms"][arm][key]


print(f"group {G}: {'arm':26s} {'d176 s1':>8s} {'d176 s2':>8s} {'d352 s1':>8s} {'d352 s2':>8s}"
      f" {'d176':>6s} {'d352':>6s} {'change':>7s}")
for label, kind, arm, key in ROWS:
    v = {w: [val(kind, n, arm, key) for n in NAMES[w]] for w in NAMES}
    m = {w: sum(v[w]) / 2 for w in v}
    print(f"          {label:26s} " + " ".join(f"{x:8.1f}" for x in v[176] + v[352])
          + f" {m[176]:6.1f} {m[352]:6.1f} {m[176] - m[352]:+7.1f}")
