#!/usr/bin/env python3
"""The refusal, explanation and fit arms, scored on the CALCULATOR'S DECODER.

    .venv-tok/bin/python tools/eval/score_int8_arms.py <model.bin> <label> [out.json]

score_arms.py produced every number FACTS.md quotes for these arms, and it differs from the device
on three axes at once: fp32 weights instead of int8, sampling at temperature 0.8 instead of greedy,
and the split's own prompt text instead of the prompt ask_build + ns_assemble build (the split's
record spans predate the change that put the asked quantity's unit in the span). This file keeps
the GRADERS -- score_arms.refusal_strict and score_arms.explain_ok, imported, not copied -- and
changes only the three things that describe the device: prompts through build/devasm, decoding
through build/int8gen (int8, greedy, the app's tool loop).

Items whose record is not in the shipped store cannot be assembled by the device and are COUNTED
AND PRINTED as unreachable, never silently dropped (fit_ho / explain_ho use held-out records by
design, so they may be partly or wholly unreachable).
"""
import json, os, pathlib, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/eval"))
import score_arms                                             # noqa: E402  (graders, imported)
import score_correct as S                                     # noqa: E402  (devasm, RID)

REFUSE = ("d1", "d1_zero", "fit", "fit_ho")
EXPLAIN = ("explain", "explain_ho")


def items(arm):
    if arm == "refuse":
        sel = json.loads((ROOT / "corpus/split_select.json").read_text())
        return [i for i in sel if i["expect"] == "refuse"]
    return json.loads((ROOT / f"corpus/split_{arm}.json").read_text())


def int8gen(model, prompts):
    p = subprocess.run([os.environ.get("INT8GEN", str(ROOT / "build/int8gen")), str(model),
                        str(ROOT / "build/tok4096.tok")],
                       input="".join(pr + "\n" for pr in prompts), capture_output=True, text=True,
                       check=True)
    lines = [l for l in p.stdout.splitlines() if l.startswith("GEN\t")]
    if len(lines) != len(prompts):
        raise SystemExit(f"ABORT: int8gen returned {len(lines)} lines for {len(prompts)} prompts")
    return [l.split("\t")[1].replace("\\n", "\n").replace("\\t", "\t").replace("\\\\", "\\")
            for l in lines]


def control(arm, grader, prompts):
    """On the DEVICE-shaped prompts, before any model output is graded: the right kind of document
    must pass and the wrong kind must fail, on every item. A grader that cannot separate them on
    this prompt shape would be measuring the shape, not the model."""
    if grader is score_arms.explain_ok:
        good = [f"<a> {p.split('</q><r>', 1)[1].split(' | ', 1)[0]} relates these quantities.<end>"
                for p in prompts]
        bad = [score_arms.CANON_REFUSAL] * len(prompts)
    else:
        good = [score_arms.CANON_REFUSAL] * len(prompts)
        bad = [score_arms.CANON_ANSWER] * len(prompts)
    pos = sum(bool(grader(p, g)) for p, g in zip(prompts, good))
    neg = sum(not grader(p, b) for p, b in zip(prompts, bad))
    print(f"  control {arm:10s} right kind passes {pos}/{len(prompts)}   "
          f"wrong kind fails {neg}/{len(prompts)}")
    if pos != len(prompts) or neg != len(prompts):
        raise SystemExit(f"ABORT: {arm} grader does not separate the controls on device prompts")


def main(model, label, dest=None):
    out = {"model": str(model), "label": label, "decoding": "greedy-int8", "prompt_path": "device",
           "arms": {}}
    for arm in ("d1", "d1_zero", "refuse", "fit", "fit_ho", "explain", "explain_ho"):
        its = items(arm)
        reach = [i for i in its if i["record"].split(" | ", 1)[0] in S.RID]
        n_un = len(its) - len(reach)
        if not reach:
            print(f"  {arm:10s} UNREACHABLE: 0 of {len(its)} records are in the shipped store")
            out["arms"][arm] = {"k": 0, "n": 0, "unreachable": n_un}
            continue
        prompts = S.device_prompts(reach)
        grader = score_arms.explain_ok if arm in EXPLAIN else score_arms.refusal_strict
        control(arm, grader, prompts)
        gens = int8gen(model, prompts)
        ok = [bool(grader(p, g)) for p, g in zip(prompts, gens)]
        k, n = sum(ok), len(ok)
        out["arms"][arm] = {"k": k, "n": n, "unreachable": n_un, "pct": 100 * k / n,
                            "rows": [{"prompt": p, "gen": g, "ok": o}
                                     for p, g, o in zip(prompts, gens, ok)]}
        print(f"  {arm:10s} {k:4d}/{n:<4d} = {100*k/n:5.1f}%   (unreachable, excluded and counted: "
              f"{n_un})", flush=True)
    if dest:
        pathlib.Path(dest).write_text(json.dumps(out, indent=1))
        print(f"  -> {dest}")


if __name__ == "__main__":
    main(pathlib.Path(sys.argv[1]), sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else None)
