#!/usr/bin/env python3
"""The no-model baseline BEHIND THE APP'S OWN SELECTOR: what does the model add end to end?

    .venv-tok/bin/python tools/eval/score_deterministic_e2e.py [out.json]

score_deterministic.py answers "what does the model add" with the relation SUPPLIED. The deployed
app chooses the relation itself (A125), so the question that matters is the marginal effect of the
model on the path a student actually gets. Here both paths get
the SAME selection -- build/autoasm, app.c's open_picker() -- and differ only after it:

    model          the calculator's decoder (build/int8gen), as score_endtoend.py runs it
    no model       score_deterministic.respond(): decline when a variable is missing (or the stored
                   explanation for an explain-worded question), otherwise evaluate the relation
                   with the parsed values and state the result; Form C (no relation) declines

Same items as score_endtoend.py (the development arms) and selection_holdout.py (the fresh
families, same seeds), same grading: answer items right by the device check against the item's
reference; decline items right when the output declines; explain right only if the right relation
was chosen and score_arms.explain_ok passes. Outcomes are right / wrong / declined, per path.
The model's rows are read from the saved results of those two scripts, not re-generated here.
"""
import json, pathlib, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/eval"))
import grade                                                  # noqa: E402
import score_arms                                             # noqa: E402
import score_correct as S                                     # noqa: E402
import score_deterministic as D                               # noqa: E402
import score_endtoend as E                                    # noqa: E402
import selection_holdout as H                                 # noqa: E402

FORM_C = "<a> I cannot answer that: no record matches this question.<end>"


def respond(prompt):
    rec = prompt.split("</q><r>", 1)[1]
    if rec.startswith("none |"):
        return FORM_C
    fields = rec.split(" | ")
    if "=" not in fields[0]:
        # A GLOSSARY TERM (the knowledge tier: `name | term:text | missing:none | definition |
        # fit:high`). score_deterministic.respond() assumes a relation and crashed on the first one
        # the selector chose. The no-model system does what the app does with a term: show the
        # stored definition. It is not a decline, so on a decline set it counts as an answer.
        return f"<a> {fields[3].strip() if len(fields) > 3 else fields[0]}.<end>"
    return D.respond(prompt)


def outcome(arm, prompt, gen, sel, ref):
    refused = grade.is_refusal(gen)
    if arm == "answer":
        ok = (grade.well_formed(gen) and not refused and S.call_right(gen, ref) and "<a>" in gen)
        return "correct" if ok else ("declined" if refused else "wrong")
    if arm == "explain":
        return ("correct" if sel == "right" and score_arms.explain_ok(prompt, gen)
                else "declined" if refused else "wrong")
    return "correct" if refused else "wrong"


def run(label, arm, items, refs):
    chosen = E.autoasm([it["q"] for it in items])
    counts = {"correct": 0, "wrong": 0, "declined": 0}
    for i, (it, (_, pr)) in enumerate(zip(items, chosen)):
        f = it["record"].split(" | ", 1)[0] if it.get("record") else None
        sel = E.selection(pr, f) if f else "none"
        counts[outcome(arm, pr, respond(pr), sel, refs[i] if refs else None)] += 1
    return counts


def model_counts():
    """The model path's saved outcomes, keyed like run()'s labels."""
    dev = json.loads((ROOT / "results/endtoend_g88p_a157.json").read_text())["arms"]
    fresh = json.loads((ROOT / "results/selection_holdout.json").read_text())["end_to_end"]
    out = {a: {k: dev[a]["result"].get(k, 0) for k in ("correct", "wrong", "declined")} for a in dev}
    for label, cell in fresh.items():
        if "A157" in cell:
            out[f"fresh: {label}"] = {k: cell["A157"][k] for k in ("correct", "wrong", "declined")}
    return out


def main(dest=None):
    tables = {a: S.load(a) for a in E.ANSWER}
    jobs = [(a, "answer", [it for it, _ in tables[a][0]], [r for _, r in tables[a][0]]) for a in E.ANSWER]
    jobs += [(a, "decline", json.loads((ROOT / f"corpus/split_{a}.json").read_text()), None)
             for a in E.DECLINE + ("fit",)]
    jobs.append(("explain", "explain", json.loads((ROOT / "corpus/split_explain.json").read_text()), None))
    stems = json.loads((ROOT / "corpus/d3_stems.json").read_text())["stems"][:E.OOS_N]
    jobs.append(("out_of_scope", "decline", [{"q": q, "record": None} for q in stems], None))
    for label, items in H.fresh().items():
        refs = [S.reference(it) for it in items]
        ok = [(it, r) for it, r in zip(items, refs) if r is not None]
        jobs.append((f"fresh: {label}", "answer", [it for it, _ in ok], [r for _, r in ok]))
    model = model_counts()
    out = {"selector": "A157 (build/autoasm)", "rows": {}}
    print(f"\n  {'item set':34s} {'n':>5s}   model right/wrong/declined   no model right/wrong/declined")
    for label, arm, items, refs in jobs:
        det = run(label, arm, items, refs)
        m = model.get(label)
        out["rows"][label] = {"n": len(items), "model": m, "no_model": det}
        fm = f"{m['correct']:4d} / {m['wrong']:3d} / {m['declined']:4d}" if m else "   (not in saved results)"
        print(f"  {label:34s} {len(items):5d}   {fm:>26s}   "
              f"{det['correct']:4d} / {det['wrong']:3d} / {det['declined']:4d}", flush=True)
    if dest:
        pathlib.Path(dest).write_text(json.dumps(out, indent=1))
        print(f"  -> {dest}")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else None)
