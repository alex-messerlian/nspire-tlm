#!/usr/bin/env python3
"""The extra-value set, split by WHERE its irrelevant value sits.

    python3 tools/eval/spare_position.py results/correct_int8_<engine>_ship.json

The paired experiment (paired_distractor.py) inserts one irrelevant value into the values-given
items. This is the independent check: answer_x is a SEPARATE set, built by answer_control.py, whose
irrelevant given is shuffled among the relevant ones -- so its position varies item to item, and
the saved device-check verdicts can be grouped by it without generating anything.

Corrects a claim that stood in FACTS and RESULT_CORRECTNESS: that answer_x "places its spare first".
answer_control shuffles the order (`rng.shuffle(order)`); measured here, the spare is first in 47 of
120 items, in the middle in 32 and last in 41.
"""
import json, pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
GIVEN = re.compile(r"\b([A-Za-z][A-Za-z0-9_]*)\s*=\s*[-+]?\d")


def main(path):
    items = json.loads((ROOT / "corpus/split_answer_x.json").read_text())
    rows = json.loads(pathlib.Path(path).read_text())["arms"]["answer_x"]["rows"]
    if len(rows) != len(items):
        raise SystemExit(f"ABORT: {len(rows)} saved rows for {len(items)} items")
    tab = {}
    for it, r in zip(items, rows):
        # the saved rows must be THESE items, in this order: the record each prompt carries
        if f"<r>{it['record'].split(' | ', 1)[0]} |" not in r["prompt"]:
            raise SystemExit(f"ABORT: saved row does not carry item's record: {it['q'][:60]}")
        names = [m.group(1) for m in GIVEN.finditer(it["q"])]
        if it.get("spare") not in names:
            raise SystemExit(f"ABORT: spare {it.get('spare')} not among the givens of {it['q'][:60]}")
        i = names.index(it["spare"])
        k = "first" if i == 0 else "last" if i == len(names) - 1 else "middle"
        t = tab.setdefault(k, [0, 0]); t[0] += bool(r["device_ok"]); t[1] += 1
    print(f"{pathlib.Path(path).name}: answer_x by position of the irrelevant value (device check)")
    for k in ("first", "middle", "last"):
        ok, n = tab.get(k, [0, 0])
        print(f"  {k:6s} right {ok:3d}/{n:<3d} = {100 * ok / max(n, 1):5.1f}%")


if __name__ == "__main__":
    main(sys.argv[1])
