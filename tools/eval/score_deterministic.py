#!/usr/bin/env python3
"""The DETERMINISTIC BASELINE: the runtime's own parse and bind, the evaluator, and no model.

    .venv-tok/bin/python tools/eval/score_deterministic.py [out.json]

What does the language model add once the runtime already holds the relation, the parsed values and
the list of unbound variables? This answers it by running
the obvious non-neural system on the same items, through the same device prompt (build/devasm), and
grading it with the SAME graders as the model:

    record lists a missing variable, compute intent  -> decline, naming the first missing variable
    record lists a missing variable, explain intent  -> the stored explanation for the record
                                                        (corpus/knowledge/explanations.json, first
                                                        variant: AI-drafted, not human-reviewed)
    nothing missing                                  -> evaluate the relation with the device-parsed
                                                        values and the store's constants, and state
                                                        the result

Intent is a keyword rule, stated here so it can be attacked: explain iff the question matches
EXPLAIN below. Everything else is compute.

What this baseline CANNOT do is stated by the same numbers: it has no way to notice that a fully
bound record does not answer the question (the `fit` set), exactly like the model.
"""
import json, pathlib, re, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/eval"))
import grade                                                  # noqa: E402
import score_arms                                             # noqa: E402
import score_correct as S                                     # noqa: E402
import score_int8_arms as A                                   # noqa: E402

EXPLAIN = re.compile(r"\b(explain|define|describe|meaning|relationship|what does|how (is|do|does|are))\b", re.I)
EXPL = {e["formula"]: e["variants"][0] for e in
        json.loads((ROOT / "corpus/knowledge/explanations.json").read_text()) if e.get("variants")}


def respond(prompt):
    q = prompt.split("<q>", 1)[1].split("</q>", 1)[0]
    fields = prompt.split("</q><r>", 1)[1].split(" | ")
    formula = fields[0]
    missing = next((f.split(":", 1)[1].strip() for f in fields if f.startswith("missing:")), "none")
    if missing != "none":
        if EXPLAIN.search(q) and formula in EXPL:
            return f"<a>{EXPL[formula]}<end>"
        first = missing.split(",")[0].split()[0]
        return f"<a>I cannot answer that: {first} is not given.<end>"
    expr = S.substituted({"q": q, "record": prompt.split("</q><r>", 1)[1]})
    call = f"<tool>eval<arg>{expr}</tool>"
    res = S.evalcli(expr)
    return f"{call}<res>{res}</res><a> {res}.<end>"


def main(dest=None):
    out = {}
    for arm in S.ARMS:
        sc, _ = S.load(arm, device=True)
        ok = sum(bool(S.correct(respond(it["prompt"]), ref)) for it, ref in sc)
        out[arm] = {"k": ok, "n": len(sc)}
        print(f"  {arm:10s} right {ok:3d}/{len(sc)} = {100 * ok / len(sc):5.1f}%")
    for arm in ("d1", "d1_zero", "fit", "explain"):
        its = [i for i in A.items(arm) if i["record"].split(" | ", 1)[0] in S.RID]
        prompts = S.device_prompts(its)
        grader = score_arms.explain_ok if arm == "explain" else score_arms.refusal_strict
        ok = sum(bool(grader(p, respond(p))) for p in prompts)
        out[arm] = {"k": ok, "n": len(prompts)}
        print(f"  {arm:10s} pass  {ok:3d}/{len(prompts)} = {100 * ok / len(prompts):5.1f}%")
    if dest:
        pathlib.Path(dest).write_text(json.dumps(out, indent=1))
        print(f"  -> {dest}")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else None)
