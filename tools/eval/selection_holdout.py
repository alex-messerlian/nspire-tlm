#!/usr/bin/env python3
"""The selection rules on questions they were NOT designed on.

    .venv-tok/bin/python tools/eval/selection_holdout.py [out.json]

A156 and A157 were designed while reading the evaluation items they are then scored on
(docs/RESULT_ENDTOEND.md), so their gains there are not held-out estimates. This draws FRESH items
from the same producers the evaluation arms come from -- tools/eval/answer_control.py (values
given, symbol asked) and tools/eval/worded_control.py (named in words; symbol only) -- with seeds
no arm uses, and scores three versions of the app's selection on them:

    A155  word coverage only                   src/store/askparse.c at 793a551
    A156  + the givens bind one relation       src/store/askparse.c at 60b3607
    A157  + the asked symbol breaks a tie      the working tree

Each version is the SAME tools/eval/autoasm.c (app.c's open_picker, not a copy) linked against that
revision's askparse.c; nothing else differs. Then the current app end to end on the fresh items,
through score_endtoend's own path (build/autoasm + build/int8gen, device-check grading).

What this controls: tuning to the particular items. What it does not: the generator's template
family, and questions written in the store's own symbols -- the same family and symbols the arms
use. Items whose question duplicates an arm item, or whose record the shipped store lacks (no
selector can return it, so it is not a trial), are excluded and counted.
"""
import json, os, pathlib, subprocess, sys, tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/eval"))
import answer_control, worded_control                         # noqa: E402  (the arms' producers)
import score_correct as S                                     # noqa: E402
import score_endtoend as E                                    # noqa: E402

SEEDS = {"values given, symbol asked": ("answer", 1011), "named in words": ("worded", 1017),
         "symbol only": ("symbol", 2017)}
REVS = {"A155": "793a551", "A156": "60b3607", "A157": None}
N = 300
HOST_LINK = ["src/store/gfx.c", "src/store/chatstore.c", "src/store/pickui.c", "src/store/picker.c",
             "src/store/assemble.c", "src/store/loader.c", "build/hoststub.o"]


def fresh():
    seen = {it["q"] for arm in ("answer_0", "answer_s", "answer_w", "answer_x")
            for it in json.loads((ROOT / f"corpus/split_{arm}.json").read_text())}
    sets = {}
    for label, (kind, seed) in SEEDS.items():
        items = (answer_control.build(n=N, seed=seed, spare=False) if kind == "answer"
                 else worded_control.build(n=N, seed=seed, worded=(kind == "worded")))
        dup = [it for it in items if it["q"] in seen]
        gone = [it for it in items if it["q"] not in seen
                and it["record"].split(" | ", 1)[0] not in S.RID]
        keep = [it for it in items if it["q"] not in seen and it["record"].split(" | ", 1)[0] in S.RID]
        print(f"  {label:27s} drawn {len(items)}, duplicates of arm items {len(dup)}, record not in "
              f"the shipped store {len(gone)} (excluded and counted), kept {len(keep)}")
        sets[label] = keep
    return sets


def autoasm_for(rev, tmp):
    if rev is None:
        return str(ROOT / "build/autoasm")
    src = pathlib.Path(tmp) / f"askparse_{rev}.c"
    src.write_text(subprocess.run(["git", "show", f"{rev}:src/store/askparse.c"], cwd=ROOT,
                                  capture_output=True, text=True, check=True).stdout)
    out = pathlib.Path(tmp) / f"autoasm_{rev}"
    subprocess.run(["cc", "-O2", "-DTLM_HOST", "-Isrc/store", "-Itools/eval", "-o", str(out),
                    "tools/eval/autoasm.c", *HOST_LINK[:4], str(src), *HOST_LINK[4:], "-lm"],
                   cwd=ROOT, check=True, capture_output=True)
    return str(out)


def select(binary, items):
    p = subprocess.run([binary], cwd=ROOT, input="".join(it["q"] + "\n" for it in items),
                       capture_output=True, text=True, check=True)
    rows = [l.split("\t", 1) for l in p.stdout.splitlines()]
    assert len(rows) == len(items) and not any(r[0].startswith("ERR") for r in rows)
    return [E.selection(pr, it["record"].split(" | ", 1)[0]) for (_, pr), it in zip(rows, items)]


def main(dest=None):
    sets = fresh()
    out = {"seeds": SEEDS, "revisions": REVS, "selection": {}, "end_to_end": {}}
    with tempfile.TemporaryDirectory() as tmp:
        bins = {name: autoasm_for(rev, tmp) for name, rev in REVS.items()}
        print(f"\n  {'item set':27s} " + "   ".join(f"{n}: right/wrong/none" for n in REVS))
        for label, items in sets.items():
            cells = {}
            for name, b in bins.items():
                sel = select(b, items)
                cells[name] = {k: sel.count(k) for k in ("right", "wrong", "none")}
            out["selection"][label] = {"n": len(items), **cells}
            print(f"  {label:27s} n={len(items):3d}  " + "   ".join(
                f"{c['right']:3d}/{c['wrong']:2d}/{c['none']:3d}" for c in cells.values()))
    print("\n  end to end, current app (A157), calculator's decoder:")
    for label, items in sets.items():
        refs = [S.reference(it) for it in items]
        ok = [(it, r) for it, r in zip(items, refs) if r is not None]
        rows = E.run_arm("answer_0", [it for it, _ in ok], [r for _, r in ok])
        res = {k: sum(x["result"] == k for x in rows) for k in ("correct", "declined", "wrong")}
        out["end_to_end"][label] = {"n": len(rows), "unscoreable": len(items) - len(ok), **res}
        print(f"  {label:27s} n={len(rows):3d}  right {res['correct']:3d} "
              f"({100 * res['correct'] / len(rows):5.1f}%)  declined {res['declined']:3d}  "
              f"wrong {res['wrong']:2d}   (unscoreable, excluded and counted: {len(items) - len(ok)})")
    if dest:
        pathlib.Path(dest).write_text(json.dumps(out, indent=1))
        print(f"  -> {dest}")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else None)
