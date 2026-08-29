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


def build(n=40, seed=7):
    import importlib.util, contextlib, io
    spec = importlib.util.spec_from_file_location("gen", ROOT / "corpus/generate.py")
    g = importlib.util.module_from_spec(spec)
    with contextlib.redirect_stdout(io.StringIO()):
        spec.loader.exec_module(g)
    rng = random.Random(seed)
    recs = g.recs
    lhs = lambda r: r["f"].split("=", 1)[0].strip()
    out = []
    for _ in range(n * 40):
        if len(out) >= n:
            break
        want, shown = rng.choice(recs), rng.choice(recs)
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
        miss = g._device_missing_mod(shown, set(vals))
        if miss != "none":
            continue
        um = " ".join(f"{v}:{shown['units'][v]}" for v in svs if v in shown.get("units", {}))
        # REUSE THE GENERATOR'S OWN QUESTION PATH. My first version wrote "Find {name}", and name
        # is a RELATION name, not a quantity -- it produced "Find Law of reflection.", which is
        # ill-posed for reasons unrelated to fit, so a refusal would not have meant what the arm
        # claims. quantity_surface + ask_for are the reviewed frames the corpus itself uses.
        q = g.ask_for(want, g.quantity_surface(want, rng), rng) + " " + \
            ", ".join(f"{v} = {vals[v]}" for v in svs) + "."
        rec = (f"{shown['f']} | {um} | missing:none | "
               f"{shown.get('req', 'standard conditions')} | fit:high")
        out.append({"q": q, "record": rec, "expect": "refuse",
                    "asked": lhs(want), "shown": lhs(shown)})
    return out


if __name__ == "__main__":
    items = build()
    p = ROOT / "corpus/split_fit.json"
    p.write_text(json.dumps(items, indent=1))
    print(f"  built {len(items)} pure-fit items -> {p.relative_to(ROOT)}")
    print(f"  every one: a fully-bound record that computes the WRONG quantity, missing:none TRUE")
    print(f"  example  {items[0]['q'][:76]}")
    print(f"           {items[0]['record'][:76]}")
