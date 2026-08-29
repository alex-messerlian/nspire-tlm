#!/usr/bin/env python3
"""Does the model judge whether a record FITS, or only whether its variables are BOUND?

The distinction is the whole of D2. src/store/assemble.c shows whatever the picker returned, and
the picker does not judge fit -- so at serve time a wrong record arrives looking exactly like a
right one, and refusing it requires reading what it MEANS.

The corpus cannot teach that, and the reason is measurable. D1 (a withheld given) and D2 (a wrong
record) both emit `missing:<var>`: 100.0% of D1 and 99.0% of D2. ANSWERABLE emits `missing:none`
100.0% of the time. So over the whole training corpus,

    refuse  <=>  missing != none

is a PERFECT classifier, and a model that learns only that scores full marks on every D2 item whose
record has an unbound variable -- which is 99% of them. The judgement is never required.

This builds the 1% case as its own arm: a record that does NOT compute the asked-for quantity, whose
variables ARE all supplied, so `missing:none` is TRUE rather than hardcoded. The cue points at
"answer" and only reading the record says otherwise. That is the pure test, and the corpus contains
5 documents of it.

Reported beside the ordinary refuse arm, never merged into it -- they measure different things.
"""
import json, pathlib, random, re, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "corpus"))
VAR = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
RESV = {"pi", "e", "sin", "cos", "tan", "asin", "acos", "atan", "sqrt", "log", "ln", "exp", "abs"}


def build(n=40, seed=7, source="shipped", matched=False):
    """source="shipped": the wrong record is one the model has seen in training.
       source="holdout": the wrong record is one it has NEVER seen, in any document.
       matched=True: the question ALSO supplies the asked-for record's own variables, which is
                     what the A42 training documents do.

    THE MATCHED ARM EXISTS BECAUSE A42 AND THE BARE ARM HAVE DIFFERENT QUESTION SHAPES. An A42
    document is built from the CORRECT record, keeps its givens, and then adds the wrong record's
    variables -- so 99.1% of them supply a variable that is absent from the shown record, and their
    questions average 86 characters. The bare arm draws only the shown record's variables: 0% carry
    an outside variable, mean length 56.

    That gap makes a NULL result ambiguous. A model that learned "a variable appears that the record
    does not use -> refuse" would score well on training and ZERO on the bare arm, and "the dose was
    too small" and "the arm does not present the shape the corpus taught" would be indistinguishable.
    A POSITIVE result on the bare arm is unambiguous, because no shortcut is shared with it.

    So both are scored. The matched arm is device-faithful -- a real user's question carries their
    own givens whatever the picker returns -- and the bare arm tests whether anything generalises
    past the shape.

    THE SHIPPED ARM IS NOT HELD OUT AT THE RECORD LEVEL, and that is measured: 61% of the n=40
    arm's shown records already appear in A42 training documents at a 10k sample, and at volume
    (2,736 A42 documents over 164 records) it approaches 100%. So a model could score on the
    shipped arm by learning PAIRWISE that record X does not answer a question about Y, without
    acquiring the general judgement. The holdout arm forecloses that: its 26 records are carved
    out of the store and never generated, so refusing one requires reading it.

    Both are reported. The shipped arm is the PRIMARY because the 0/40 baseline was measured on
    shipped records and a primary outcome may not change its instrument between arms; the holdout
    arm is the stronger test and is pre-named as such."""
    import importlib.util, contextlib, io
    spec = importlib.util.spec_from_file_location("gen", ROOT / "corpus/generate.py")
    g = importlib.util.module_from_spec(spec)
    with contextlib.redirect_stdout(io.StringIO()):
        spec.loader.exec_module(g)
    rng = random.Random(seed)
    if source == "holdout":
        held = json.loads((ROOT / "corpus/units_holdout.json").read_text())
        shipped = {r["f"] for r in g.recs}
        pool = [r for r in held if r["f"] not in shipped]
        assert pool, "holdout pool empty -- a source that silently falls back is a guess"
    else:
        pool = g.recs
    recs = g.recs                      # WANT always comes from the shipped store: the question
                                       # must be about a quantity the model knows, so that a
                                       # failure is about the RECORD and not the question.
    lhs = lambda r: r["f"].split("=", 1)[0].strip()
    out = []
    for _ in range(n * 40):
        if len(out) >= n:
            break
        want, shown = rng.choice(recs), rng.choice(pool)
        if lhs(want) == lhs(shown) or g.lhs_unit(want) == g.lhs_unit(shown):
            continue
        svs = [v for v in dict.fromkeys(VAR.findall(shown["f"].split("=", 1)[1]))
               if v not in RESV and v != lhs(shown)]
        svs = [v for v in svs if g._const_for(shown, v) is None]
        if not svs or len(svs) > 3:
            continue
        vals = {}
        for v in svs:
            rr = g.quantity_range(shown, v)
            x = g.sample_in_range(rng, rr[0], rr[1], rr[2]) if rr else g.sample_value(rng)
            vals[v] = g._num(x) if hasattr(g, "_num") else x
        # THE QUESTION ASKS FOR want's QUANTITY AND SUPPLIES shown's VARIABLES, so the shown record
        # is fully bound and still cannot answer. missing:none is now TRUE, not asserted.
        if matched:
            for v in dict.fromkeys(VAR.findall(want["f"].split("=", 1)[1])):
                if v in RESV or v == lhs(want) or v in vals:
                    continue
                if g._const_for(want, v) is not None:
                    continue
                rr = g.quantity_range(want, v)
                vals[v] = (g._num(g.sample_in_range(rng, rr[0], rr[1], rr[2])) if rr
                           else g._num(g.sample_value(rng)))
            svs = list(vals)
            rng.shuffle(svs)
        miss = g._device_missing_mod(shown, set(vals))
        if miss != "none":
            continue
        # THE ASKED QUANTITY MUST NOT BE ONE OF THE GIVENS. Three of 120 shipped items and one of
        # 120 holdout items asked for a symbol the question itself supplied -- "Estimate velocity
        # of a wave. B = 17.9, l = 0.102, v = 31.48." A model answering 31.48 there is not failing
        # to judge fit, and a model refusing is not necessarily judging it either. The item cannot
        # discriminate, so it is not an item.
        if lhs(want) in vals:
            continue
        um = " ".join(f"{v}:{shown['units'][v]}"
                      for v in dict.fromkeys(VAR.findall(shown["f"].split("=", 1)[1]))
                      if v in shown.get("units", {}))
        # REUSE THE GENERATOR'S OWN QUESTION PATH. My first version wrote "Find {name}", and name
        # is a RELATION name, not a quantity -- it produced "Find Law of reflection.", which is
        # ill-posed for reasons unrelated to fit, so a refusal would not have meant what the arm
        # claims. quantity_surface + ask_for are the reviewed frames the corpus itself uses.
        # THE ARM'S QUESTION SURFACE MUST BE THE CORPUS'S. Composing it here produced bare givens
        # -- `Find E. m = 1, c = 2.` -- a surface that occurs in 0 of 9,994 training documents,
        # because every one of the 16 GIVE frames has a lead-in. compose_question is the generator's
        # own path and is now shared rather than mirrored.
        ask = g.ask_for(want, g.quantity_surface(want, rng), rng)
        q = g.compose_question(ask, ", ".join(f"{v} = {vals[v]}" for v in svs), rng)
        rec = (f"{shown['f']} | {um} | missing:none | "
               f"{shown.get('req', 'standard conditions')} | fit:high")
        out.append({"q": q, "record": rec, "expect": "refuse",
                    "asked": lhs(want), "shown": lhs(shown)})
    return out


if __name__ == "__main__":
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 120
    for src, mt, out in (("shipped", False, "corpus/split_fit.json"),
                         ("holdout", False, "corpus/split_fit_ho.json"),
                         ("shipped", True,  "corpus/split_fit_m.json")):
        items = build(n=n, source=src, matched=mt)
        (ROOT / out).write_text(json.dumps(items, indent=1))
        shown = {i["record"].split(" | ")[0] for i in items}
        print(f"  {src:8s}{' matched' if mt else '       '} n={len(items):4d}  "
              f"distinct shown records {len(shown):3d}  -> {out}")
        print(f"           {items[0]['q'][:74]}")
        print(f"           {items[0]['record'][:74]}")
