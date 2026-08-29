#!/usr/bin/env python3
"""THE MATCHED ANSWER CONTROL. Does the model judge fit, or refuse anything with a spare variable?

POST-HOC DIAGNOSTIC, NOT A PRE-REGISTERED OUTCOME. It is built after seeing fit_m = 98.9% against
fit = 7.2%, and it is labelled that way wherever it is reported.

The retrain scores 98.9% on the arm whose questions carry a variable the shown record does not use,
and 7.2% on the arm whose questions do not. Two readings fit that:

  (a) it learned to judge whether the record answers the question, and the matched arm is simply
      the shape it was taught in;
  (b) it learned "a variable appears that this record does not use -> refuse", which is a surface
      rule that happens to be right on every item of the matched arm.

"Refuses more" and "judges fit" are the same measurement until something separates them. This does:
an ANSWERABLE question -- the shown record computes exactly what is asked, every variable bound --
with ONE extra irrelevant given appended. Reading (a) answers it. Reading (b) refuses it.

The extra given is drawn from a DIFFERENT record and named so it cannot collide with the shown
record's variables.
"""
import contextlib, importlib.util, io, json, pathlib, random, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "corpus"))
VAR = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
RESV = {"pi", "e", "sin", "cos", "tan", "asin", "acos", "atan", "sqrt", "log", "ln", "exp", "abs"}


def build(n=120, seed=11, spare=True):
    spec = importlib.util.spec_from_file_location("gen", ROOT / "corpus/generate.py")
    g = importlib.util.module_from_spec(spec)
    with contextlib.redirect_stdout(io.StringIO()):
        spec.loader.exec_module(g)
    rng = random.Random(seed)
    lhs = lambda r: r["f"].split("=", 1)[0].strip()
    out = []
    for _ in range(n * 40):
        if len(out) >= n:
            break
        r = rng.choice(g.recs)
        svs = [v for v in dict.fromkeys(VAR.findall(r["f"].split("=", 1)[1]))
               if v not in RESV and v != lhs(r) and g._const_for(r, v) is None]
        if not svs or len(svs) > 3:
            continue
        vals = {}
        for v in svs:
            rr = g.quantity_range(r, v)
            vals[v] = g._num(g.sample_in_range(rng, rr[0], rr[1], rr[2]) if rr else g.sample_value(rng))
        # ONE SPARE GIVEN from a different record, not used by the shown record.
        # spare=False builds the SAME arm without it, which is what keeps the V3 contrast clean:
        # once the split's answer items carried spares (A43k), `answer` and `answer_x` measured the
        # same thing -- 89.2% and 76.9% wrongly refused on retrain 1 -- and two arms that differ
        # only in degree are one arm. answer_0 is the no-spare cell.
        if not spare:
            if g._device_missing_mod(r, set(vals)) != "none":
                continue
            order = list(vals); rng.shuffle(order)
            ask = g.ask_for(r, g.quantity_surface(r, rng), rng)
            q = g.compose_question(ask, ", ".join(f"{v} = {vals[v]}" for v in order), rng)
            um = " ".join(f"{v}:{r['units'][v]}" for v in svs if v in r.get("units", {}))
            out.append({"q": q,
                        "record": f"{r['f']} | {um} | missing:none | "
                                  f"{r.get('req', 'standard conditions')} | fit:high",
                        "expect": "answer", "asked": lhs(r), "shown": lhs(r), "spare": None})
            continue
        # `cand`, NOT `spare`. The candidate list was called `spare` and SHADOWED THE PARAMETER of
        # the same name: after the first iteration the flag was gone, so `if not spare:` tested the
        # list, the no-spare branch stopped firing, and the arm built 120 items with 0.0% spare
        # givens while reporting itself as the spare arm. The baseline moved 76.9% -> 6.1% and the
        # only thing that caught it was re-measuring what the items actually contained.
        #
        # One name for two things, which this repo has on record from the `res` span defect.
        other = rng.choice(g.recs)
        cand = [v for v in dict.fromkeys(VAR.findall(other["f"].split("=", 1)[1]))
                if v not in RESV and v not in vals and v != lhs(r)
                and v not in VAR.findall(r["f"])]
        if not cand:
            continue
        sv = cand[0]
        rr = g.quantity_range(other, sv)
        vals[sv] = g._num(g.sample_in_range(rng, rr[0], rr[1], rr[2]) if rr else g.sample_value(rng))
        # the record is still fully bound and still computes exactly what is asked
        if g._device_missing_mod(r, set(vals)) != "none":
            continue
        order = list(vals); rng.shuffle(order)
        ask = g.ask_for(r, g.quantity_surface(r, rng), rng)
        q = g.compose_question(ask, ", ".join(f"{v} = {vals[v]}" for v in order), rng)
        um = " ".join(f"{v}:{r['units'][v]}" for v in svs if v in r.get("units", {}))
        rec = (f"{r['f']} | {um} | missing:none | "
               f"{r.get('req', 'standard conditions')} | fit:high")
        out.append({"q": q, "record": rec, "expect": "answer",
                    "asked": lhs(r), "shown": lhs(r), "spare": sv})
    return out


if __name__ == "__main__":
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 120
    for sp, out in ((True, "corpus/split_answer_x.json"), (False, "corpus/split_answer_0.json")):
        items = build(n=n, spare=sp)
        (ROOT / out).write_text(json.dumps(items, indent=1))
        print(f"  {'spare  ' if sp else 'no spare'} n={len(items):4d} -> {out}")
        print(f"           {items[0]['q'][:76]}")
