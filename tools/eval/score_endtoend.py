#!/usr/bin/env python3
"""THE SHIPPED APP, END TO END: the app chooses the relation, then the calculator's decoder answers.

    .venv-tok/bin/python tools/eval/score_endtoend.py [out.json]

WHY. Every quality number before this one supplied the right relation (build/devasm takes the
record id), which was the app's behaviour while the student picked from a list. Since A125 the app
picks the relation itself and declines when it is not confident, so a student's answer is the
product of two stages. This scores both, on the same items as score_correct / score_int8_arms:

  selection   build/autoasm: app.c's open_picker(), the shipped code, not a copy; then the prompt
              app_request assembles for whatever it chose (Form C when it chose nothing)
  generation  build/int8gen: the shipped int8 file through the app's own generation loop (gencore.c)

PER ITEM, TWO OUTCOMES, printed separately so neither hides inside the other:
  selection   right  the prompt's record is the item's relation (formula string equal)
              wrong  the app chose another record
              none   the app declined to choose (Form C)
  result      the arm's own criterion, applied to what the student would see:
    answer_*  correct  = score_correct's DEVICE CHECK against the item's reference (call result
                         within 0.1% of the reference AND the shipped 2% prose check). Judged against
                         the REFERENCE, never against the chosen record, so a wrong choice that
                         happens to produce the right number would count -- and is counted apart.
              declined = a refusal (grade.is_refusal on the generation)
              wrong    = anything else: a stated answer that is not the right one
    d1, d1_zero  correct = declined (a needed value is withheld; the right output is a refusal)
    explain      correct = the right relation was chosen AND score_arms.explain_ok passes
    out_of_scope correct = declined. 2,000 certified out-of-scope stems (corpus/d3_stems.json, the
                 first 2,000). NOTE: the D3 training class draws its questions from this same pool,
                 so the MODEL's refusal here is in-distribution; the SELECTION is not trained.
    fit          correct = declined. The VALUE-CONTAINING NEGATIVE SET (A158): each question's values
                 bind a relation exactly (the item's `record`) but it asks for another quantity.
                 That is the input on which A156/A157 could pick a relation confidently and
                 wrongly, so "selection right" is reported for it as `chose_inapplicable`.

CONTROLS, before any model output is graded: the answer grader is score_correct's, whose positive
and negative controls run first (score_correct.controls); and the selection grader must mark every
item "right" when handed the item's own record span, and "wrong" when handed another's.
"""
import json, os, pathlib, re, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/eval"))
import grade                                                  # noqa: E402
import score_arms                                             # noqa: E402  (explain_ok, imported)
import score_correct as S                                     # noqa: E402  (reference, device check)

ANSWER = ("answer_0", "answer_s", "answer_w", "answer_x")
DECLINE = ("d1", "d1_zero")
MODEL = ROOT / "build/transfer/model4096.bin.tns"
OOS_N = 2000


def autoasm(questions, binary=None):
    """[(rid or None, prompt)] -- the app's own choice and the prompt it would send. `binary` is an
    autoasm linked against another revision's askparse.c (selection_holdout.py); default: current."""
    p = subprocess.run([binary or str(ROOT / "build/autoasm")], cwd=ROOT,
                       input="".join(q.replace("\n", " ") + "\n" for q in questions),
                       capture_output=True, text=True, check=True)
    out = p.stdout.splitlines()
    bad = [o for o in out if o.startswith("ERR")]
    if len(out) != len(questions) or bad:
        raise SystemExit(f"ABORT: autoasm returned {len(out)} lines for {len(questions)} questions, "
                         f"{len(bad)} errors ({bad[:3]})")
    rows = [o.split("\t", 1) for o in out]
    return [(None if r == "-" else r, pr) for r, pr in rows]


def shown_formula(prompt):
    """The relation the model is shown: the record span's first field ('none' for Form C)."""
    m = re.search(r"<r>(.*?) \|", prompt)
    return m.group(1) if m else None


def selection(prompt, item_formula):
    f = shown_formula(prompt)
    return "none" if f in (None, "none") else ("right" if f == item_formula else "wrong")


def int8gen(prompts):
    """[(generation, prose_states_result)] from the shipped int8 file and the app's loop."""
    p = subprocess.run([os.environ.get("INT8GEN", str(ROOT / "build/int8gen")), str(MODEL),
                        str(ROOT / "build/tok4096.tok")],
                       input="".join(pr + "\n" for pr in prompts), capture_output=True, text=True,
                       check=True)
    lines = [l.split("\t") for l in p.stdout.splitlines() if l.startswith("GEN\t")]
    if len(lines) != len(prompts):
        raise SystemExit(f"ABORT: int8gen returned {len(lines)} lines for {len(prompts)} prompts")
    unesc = lambda t: t.replace("\\n", "\n").replace("\\t", "\t").replace("\\\\", "\\")
    return [(unesc(l[1]), l[3] == "1") for l in lines]


def device_ok(gen, states, ref):
    """score_correct's DEVICE CHECK, the criterion every answer-arm number in the paper uses."""
    return (grade.well_formed(gen) and not grade.is_refusal(gen) and S.call_right(gen, ref)
            and states and "<a>" in gen)


def selection_control(items):
    """The selection grader must see the item's own record as right and another's as wrong."""
    fs = [it["record"].split(" | ", 1)[0] for it in items]
    own = sum(selection(f"<q>x</q><r>{it['record']}", f) == "right" for it, f in zip(items, fs))
    other = sum(selection(f"<q>x</q><r>{items[(i + 1) % len(items)]['record']}", f) == "wrong"
                for i, f in enumerate(fs) if fs[(i + 1) % len(items)] != f)
    n_other = sum(fs[(i + 1) % len(items)] != f for i, f in enumerate(fs))
    none = selection("<q>x</q><r>none | missing:none | no matching relation | fit:low<a>", fs[0])
    print(f"  control selection  own record right {own}/{len(items)}   another's wrong "
          f"{other}/{n_other}   Form C reads as none: {none == 'none'}")
    if own != len(items) or other != n_other or none != "none":
        raise SystemExit("ABORT: the selection grader does not separate its controls")


def tally(rows, key):
    t = {}
    for r in rows:
        t[r[key]] = t.get(r[key], 0) + 1
    return t


def run_arm(arm, items, refs=None, binary=None):
    fs = [it["record"].split(" | ", 1)[0] if it.get("record") else None for it in items]
    chosen = autoasm([it["q"] for it in items], binary)
    gens = int8gen([pr for _, pr in chosen])
    rows = []
    for i, (it, (rid, pr), (gen, states)) in enumerate(zip(items, chosen, gens)):
        sel = selection(pr, fs[i]) if fs[i] else ("none" if rid is None else "wrong")
        refused = grade.is_refusal(gen)
        if arm in ANSWER:
            ok = device_ok(gen, states, refs[i])
            res = "correct" if ok else ("declined" if refused else "wrong")
        elif arm == "explain":
            res = ("correct" if sel == "right" and score_arms.explain_ok(pr, gen)
                   else "declined" if refused else "wrong")
        else:                                        # d1, d1_zero, out_of_scope: declining is right
            res = "correct" if refused else "wrong"
        rows.append({"q": it["q"], "rid": rid, "prompt": pr, "gen": gen, "selection": sel,
                     "result": res, "ref": refs[i] if refs else None})
    return rows


def main(dest=None):
    tables = {arm: S.load(arm) for arm in ANSWER}
    S.controls(tables)                       # answer grader: positive and negative controls
    selection_control([it for arm in ANSWER for it, _ in tables[arm][0]])

    out = {"model": str(MODEL.relative_to(ROOT)), "decoding": "greedy-int8",
           "selection": "app.c open_picker (build/autoasm)", "arms": {}}
    print(f"\n  {'arm':12s} {'n':>5s}  {'choice right':>12s} {'wrong':>6s} {'none':>6s}   "
          f"{'result correct':>14s} {'declined':>9s} {'wrong':>6s}")
    arms = [(a, [it for it, _ in tables[a][0]], [r for _, r in tables[a][0]]) for a in ANSWER]
    arms += [(a, json.loads((ROOT / f"corpus/split_{a}.json").read_text()), None)
             for a in DECLINE + ("explain", "fit")]
    stems = json.loads((ROOT / "corpus/d3_stems.json").read_text())["stems"][:OOS_N]
    arms.append(("out_of_scope", [{"q": q, "record": None} for q in stems], None))
    for arm, items, refs in arms:
        skipped = len(tables[arm][1]) if arm in tables else 0
        rows = run_arm(arm, items, refs)
        s, r, n = tally(rows, "selection"), tally(rows, "result"), len(rows)
        if arm == "fit":          # "right" here is the bound but INAPPLICABLE relation
            s = {"chose_inapplicable": s.get("right", 0), "wrong": s.get("wrong", 0),
                 "none": s.get("none", 0)}
        lucky = sum(x["selection"] != "right" and x["result"] == "correct" for x in rows
                    if arm in ANSWER)
        out["arms"][arm] = {"n": n, "unscoreable": skipped, "selection": s, "result": r,
                            "correct_without_right_choice": lucky, "rows": rows}
        pct = lambda d, k: f"{d.get(k, 0):4d} {100 * d.get(k, 0) / n:5.1f}%"
        print(f"  {arm:12s} {n:5d}  {pct(s, 'chose_inapplicable' if arm == 'fit' else 'right')} {s.get('wrong', 0):6d} {s.get('none', 0):6d}   "
              f"{pct(r, 'correct')} {r.get('declined', 0):9d} {r.get('wrong', 0):6d}"
              + (f"   (correct with another record: {lucky})" if lucky else "")
              + (f"   (unscoreable, excluded and counted: {skipped})" if skipped else ""),
              flush=True)
    if dest:
        pathlib.Path(dest).write_text(json.dumps(out, indent=1))
        print(f"  -> {dest}")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else None)
