#!/usr/bin/env python3
"""A PAIRED distractor experiment: the same question with and without one irrelevant value.

    .venv-tok/bin/python tools/eval/paired_distractor.py <model.bin> <int8gen binary> [out.json]

The draft compared the values-given set (answer_0) with a separate extra-value set (answer_x):
different items, different relations, generated along different random paths. That shows two
distributions, not the effect of adding a value.

Here every answer_0 item is scored three times, differing ONLY in one inserted assignment:
    base    the item as it is
    first   `Z = u, ` inserted before the question's first given
    last    `, Z = u` inserted after its last given
The distractor (Z, u) is a spare given taken from answer_x -- a symbol and value the generator
itself uses as an irrelevant given -- chosen by index so the run is deterministic, and never a
symbol that appears in the item's record or question. Prompts go through build/devasm and
generation through the named int8gen, as in score_correct --int8; "right" is the same
device-check criterion (call within 0.1% of the reference, prose within 2%).
"""
import json, pathlib, re, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/eval"))
import grade                                                  # noqa: E402
import score_correct as S                                     # noqa: E402

IDENT = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")


def distractors():
    out = []
    for it in json.loads((ROOT / "corpus/split_answer_x.json").read_text()):
        vals = dict(S.GIVEN.findall(it["q"]))
        if it.get("spare") and it["spare"] in vals:
            out.append((it["spare"], vals[it["spare"]]))
    return out


def variants(it, pool, k):
    """(first, last) versions of item `it`, or None if no usable distractor or no given."""
    used = set(IDENT.findall(it["record"].split(" | ", 1)[0])) | set(IDENT.findall(it["q"]))
    ms = list(S.GIVEN.finditer(it["q"]))
    if not ms:
        return None
    for j in range(len(pool)):
        z, u = pool[(k + j) % len(pool)]
        if z not in used:
            q = it["q"]
            first = q[:ms[0].start()] + f"{z} = {u}, " + q[ms[0].start():]
            last = q[:ms[-1].end()] + f", {z} = {u}" + q[ms[-1].end():]
            return dict(it, q=first), dict(it, q=last), z
    return None


def run(binary, model, items):
    prompts = S.device_prompts(items)
    p = subprocess.run([binary, str(model), str(ROOT / "build/tok4096.tok")],
                       input="".join(x + "\n" for x in prompts), capture_output=True, text=True,
                       check=True)
    rows = [l.split("\t") for l in p.stdout.splitlines() if l.startswith("GEN\t")]
    assert len(rows) == len(items), (len(rows), len(items))
    out = []
    for it, (_, t, _res, st) in zip(items, rows):
        g = t.replace("\\n", "\n").replace("\\t", "\t").replace("\\\\", "\\")
        ref = S.reference(it)
        ok = (grade.well_formed(g) and not grade.is_refusal(g) and S.call_right(g, ref)
              and st == "1" and "<a>" in g)
        out.append({"prompt": it.get("prompt"), "gen": g, "ok": bool(ok)})
    return out, prompts


def main(model, binary, dest=None):
    base = json.loads((ROOT / "corpus/split_answer_0.json").read_text())
    pool = distractors()
    trip = [(it, variants(it, pool, k)) for k, it in enumerate(base)]
    kept = [(it, v) for it, v in trip if v]
    print(f"items {len(base)}; with a usable distractor {len(kept)} (excluded and counted: "
          f"{len(base) - len(kept)})")
    B, _ = run(binary, model, [it for it, _ in kept])
    F, _ = run(binary, model, [v[0] for _, v in kept])
    L, pl = run(binary, model, [v[1] for _, v in kept])
    n = len(kept)
    res = {"model": str(model), "decoder": binary, "n": n, "conditions": {}}
    for name, cond in (("first", F), ("last", L)):
        both = sum(b["ok"] and c["ok"] for b, c in zip(B, cond))
        lost = sum(b["ok"] and not c["ok"] for b, c in zip(B, cond))
        gained = sum(not b["ok"] and c["ok"] for b, c in zip(B, cond))
        neither = n - both - lost - gained
        used = sum(1 for (it, v), c in zip(kept, cond) if not c["ok"] and
                   re.search(rf"\(\s*{re.escape(dict(S.GIVEN.findall(v[0]['q']))[v[2]])}", c["gen"] or ""))
        res["conditions"][name] = {"right": sum(c["ok"] for c in cond), "both": both, "lost": lost,
                                   "gained": gained, "neither": neither,
                                   "wrong_with_distractor_value_in_call": used}
        print(f"  distractor {name:5s}: right {sum(c['ok'] for c in cond)}/{n}   paired vs base: "
              f"kept {both}, LOST {lost}, gained {gained}, wrong both {neither}; "
              f"wrong answers with the distractor's value in the call: {used}")
    print(f"  base          : right {sum(b['ok'] for b in B)}/{n}")
    res["base_right"] = sum(b["ok"] for b in B)
    if dest:
        pathlib.Path(dest).write_text(json.dumps(res, indent=1))
        print(f"  -> {dest}")


if __name__ == "__main__":
    main(pathlib.Path(sys.argv[1]), sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else None)
