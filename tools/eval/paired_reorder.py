#!/usr/bin/env python3
"""Does the model bind values to variables by NAME or by POSITION? The reorder control.

    .venv-tok/bin/python tools/eval/paired_reorder.py <model.bin> <int8gen binary> [out.json]

The paired distractor (paired_distractor.py) showed an irrelevant value costs 4 answers placed last
and 55 placed first. Two readings fit that: the model learned WHERE irrelevant values sit, or it
binds every value by position and a leading value shifts them all. This separates them. Every
answer_0 item with two or more givens is scored twice, differing ONLY in the order of its relevant
assignments (reversed, nothing inserted). If binding were positional, reversing would break it.

Recorded first in A150 from an ad hoc run (results/paired_reorder_ship.txt, defective and group-16
engines: 83/91 -> 83/91 and 87/91 -> 87/91). This is that run as a script, so the shipped engine's
figure has a producer. Prompts through build/devasm, generation through the named int8gen, and
"right" is score_correct's device check, exactly as paired_distractor.py.
"""
import json, pathlib, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/eval"))
import score_correct as S                                     # noqa: E402
from paired_distractor import run                             # noqa: E402  (same scoring path)


def reversed_item(it):
    """The item with its assignments in reverse order, or None if it has fewer than two."""
    q = it["q"]
    ms = list(S.GIVEN.finditer(q))
    if len(ms) < 2:
        return None
    texts = [m.group(0) for m in ms]
    out, last = [], 0
    for m, t in zip(ms, reversed(texts)):
        out.append(q[last:m.start()]); out.append(t); last = m.end()
    out.append(q[last:])
    return dict(it, q="".join(out))


def main(model, binary, dest=None):
    base = json.loads((ROOT / "corpus/split_answer_0.json").read_text())
    pairs = [(it, r) for it in base if (r := reversed_item(it))]
    print(f"items {len(base)}; with two or more givens {len(pairs)} (excluded and counted: "
          f"{len(base) - len(pairs)})")
    A, _ = run(binary, model, [it for it, _ in pairs])
    R, _ = run(binary, model, [r for _, r in pairs])
    n = len(pairs)
    lost = sum(a["ok"] and not r["ok"] for a, r in zip(A, R))
    gained = sum(not a["ok"] and r["ok"] for a, r in zip(A, R))
    res = {"model": str(model), "decoder": binary, "n": n,
           "original_right": sum(a["ok"] for a in A), "reversed_right": sum(r["ok"] for r in R),
           "lost": lost, "gained": gained,
           "example": {"original": pairs[0][0]["q"], "reversed": pairs[0][1]["q"]}}
    print(f"  original order right {res['original_right']}/{n}   reversed right "
          f"{res['reversed_right']}/{n}   lost {lost}, gained {gained}")
    print(f"  e.g. {res['example']['original']}\n       {res['example']['reversed']}")
    if dest:
        pathlib.Path(dest).write_text(json.dumps(res, indent=1))
        print(f"  -> {dest}")


if __name__ == "__main__":
    main(pathlib.Path(sys.argv[1]), sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else None)
