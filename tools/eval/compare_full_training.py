"""The decision docs/PREREG_FULL_TRAINING.md fixed before the full-length run: does it replace the shipped model?

    .venv-tok/bin/python tools/eval/compare_full_training.py [NAME]

NAME is the new checkpoint's name (default full_d352); its result files are the ones the overnight
scoring writes under results/ with that name. The shipped model's files are the ones behind the
paper's tables. Both sides are scored by the same scripts, on the calculator's decoder (int8, greedy,
the app's own prompt assembly, and for end to end the app's own selector).

THE RULE, as pre-registered: the new model ships if it is at least as good on every set that should be
declined and better on answer accuracy. Written out here before any score of the new model existed
(2026-09-28), so that the reading cannot be fitted to the result:

  declines   every decline set, counted in items declined, new >= shipped:
             relation supplied: value withheld, no values, relation does not apply;
             end to end, shipped selector: the same three and the 2,000 out-of-scope questions.
  answers    correct answers summed over the four answerable sets, new > shipped, in BOTH totals:
             relation supplied (480 items), and end to end with the shipped selector (the 480
             development items plus the 1,200 fresh ones).

Everything else printed (explanations, strict accuracy, the paired and positional read-outs, the
earlier selectors) is reported, not decided on.
"""
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
R = ROOT / "results"
ANSWER = ["answer_0", "answer_s", "answer_w", "answer_x"]
LABEL = {"answer_0": "values given", "answer_s": "named by symbol", "answer_w": "named in words",
         "answer_x": "irrelevant value added", "d1": "decline: value withheld",
         "d1_zero": "decline: no values", "fit": "decline: relation does not apply",
         "explain": "explain (format)", "out_of_scope": "decline: out of scope"}
FRESH = ["values given, symbol asked", "named in words", "symbol only", "one irrelevant value"]


def load(name):
    return json.loads((R / name).read_text())


def supplied(tag):
    """Relation supplied: {arm: (k, n)}, plus strict k for the answerable arms."""
    c, a = load(f"correct_int8_g88p_{tag}.json")["arms"], load(f"arms_int8_g88p_{tag}.json")["arms"]
    out, strict = {}, {}
    for arm in ANSWER:
        n = c[arm]["n"]
        out[arm] = (round(c[arm]["device_ok_pct"] * n / 100), n)
        strict[arm] = round(c[arm]["mean_pct"] * n / 100)
    for arm in ("d1", "d1_zero", "fit", "explain"):
        out[arm] = (a[arm]["k"], a[arm]["n"])
    return out, strict


def e2e(path):
    arms = load(path)["arms"]
    return {arm: (v["result"], v["selection"], v["n"]) for arm, v in arms.items()}


def fresh(path):
    return load(path)["end_to_end"]


def main(name="full_d352"):
    old_s, old_strict = supplied("ship")
    new_s, new_strict = supplied(name)
    old_e = {v: e2e(f"endtoend_g88p_{v}.json") for v in ("a155", "a156", "a157")}
    new_e = {v: e2e(f"endtoend_g88p_{name}_{v}.json") for v in ("a155", "a156", "a157")}
    old_f, new_f = fresh("selection_holdout.json"), fresh(f"selection_holdout_{name}.json")

    print(f"shipped (train/ship.pt, 8,000 steps)  vs  {name}\n")
    print("RELATION SUPPLIED (Table 4): items passed")
    for arm in ANSWER + ["d1", "d1_zero", "fit", "explain"]:
        (k0, n), (k1, _) = old_s[arm], new_s[arm]
        extra = (f"   strict {old_strict[arm]:3d} -> {new_strict[arm]:3d}" if arm in ANSWER else "")
        print(f"  {LABEL[arm]:34s} {k0:4d} -> {k1:4d} / {n}{extra}")

    print("\nEND TO END, correct / incorrect / declined (decline sets: declined / answered)")
    for v in ("a155", "a156", "a157"):
        print(f"  selector {v.upper()}")
        for arm, (r0, _, n) in old_e[v].items():
            r1 = new_e[v][arm][0]
            f = (lambda r: f"{r.get('correct', 0):4d} / {r.get('wrong', 0):3d}" + (
                f" / {r.get('declined', 0):3d}" if arm in ANSWER + ["explain"] else ""))
            print(f"    {LABEL.get(arm, arm):32s} {f(r0)}  ->  {f(r1)}   (n={n})")
    print("  fresh sets (300 each)")
    for label in FRESH:
        for v in ("A155", "A156", "A157"):
            c0, c1 = old_f[label][v], new_f[label][v]
            print(f"    {label:28s} {v}  {c0['correct']:3d}/{c0['wrong']:2d}/{c0['declined']:3d}"
                  f"  ->  {c1['correct']:3d}/{c1['wrong']:2d}/{c1['declined']:3d}")
    fl = "fit (negative, development set)"
    if fl in old_f:
        for v in ("A155", "A156", "A157"):
            print(f"    {fl:28s} {v}  chose the inapplicable relation {old_f[fl][v]['chose_inapplicable']}"
                  f" -> {new_f[fl][v]['chose_inapplicable']}")

    # ---- the pre-registered decision ----
    print("\nDECISION (docs/PREREG_FULL_TRAINING.md)")
    ok = True
    for arm in ("d1", "d1_zero", "fit"):
        k0, k1 = old_s[arm][0], new_s[arm][0]
        good = k1 >= k0; ok &= good
        print(f"  {'pass' if good else 'FAIL'}  supplied  {LABEL[arm]:34s} {k0} -> {k1}")
    for arm in ("d1", "d1_zero", "fit", "out_of_scope"):
        k0, k1 = old_e["a157"][arm][0].get("correct", 0), new_e["a157"][arm][0].get("correct", 0)
        good = k1 >= k0; ok &= good
        print(f"  {'pass' if good else 'FAIL'}  end to end {LABEL[arm]:33s} {k0} -> {k1}")
    s0, s1 = sum(old_s[a][0] for a in ANSWER), sum(new_s[a][0] for a in ANSWER)
    good = s1 > s0; ok &= good
    print(f"  {'pass' if good else 'FAIL'}  supplied, correct answers over 480 items: {s0} -> {s1}")
    e0 = sum(old_e["a157"][a][0].get("correct", 0) for a in ANSWER) + sum(
        old_f[l]["A157"]["correct"] for l in FRESH)
    e1 = sum(new_e["a157"][a][0].get("correct", 0) for a in ANSWER) + sum(
        new_f[l]["A157"]["correct"] for l in FRESH)
    good = e1 > e0; ok &= good
    print(f"  {'pass' if good else 'FAIL'}  end to end, correct answers over 1,680 items: {e0} -> {e1}")
    print(f"\n  => {'SHIPS: the full-length model replaces the shipped one' if ok else 'DOES NOT SHIP: the shipped model stays'}")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main(*sys.argv[1:2]))
