#!/usr/bin/env python3
"""The missing cell: a record that ANSWERS the question, with one given withheld.

THE FACTORIAL CHECK FOUND THIS, NOT INTUITION. Enumerating what each arm holds constant over four
variables -- V1 does the record fit, V2 is it fully bound, V3 is a spare given present, V4 has the
model seen the record -- gives five matched pairs and one EMPTY CELL: (fits, unbound). Nothing
tested it, while D1 is 9.75% of the training corpus.

Why it matters for retrain 2. The `refuse` arm is a wrong record that is ALSO unbound, so it varies
V1 and V2 together: a model could pass it on either. Separating them needs a cell where the record
fits and only the binding is broken. That is this arm, and it is the one that says whether the
binding check survives A43 -- the fix deliberately makes a spare given uninformative, and the
binding check is the neighbouring capability most likely to be disturbed.

The model must REFUSE these: the record is right, but a value it needs was not supplied.
"""
import contextlib, importlib.util, io, json, pathlib, random, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "corpus"))
VAR = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
RESV = {"pi", "e", "sin", "cos", "tan", "asin", "acos", "atan", "sqrt", "log", "ln", "exp", "abs"}


def build(n=120, seed=13, spare=True):
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
        free = [v for v in dict.fromkeys(VAR.findall(r["f"].split("=", 1)[1]))
                if v not in RESV and v != lhs(r) and g._const_for(r, v) is None]
        if len(free) < 2:
            continue
        # WITHHOLD A FREE VARIABLE, NEVER A CONSTANT -- corpus/generate.py's A12. A constant is
        # inlined by the runtime, so refusing for want of one is a refusal on a satisfied
        # precondition, which is wrong supervision in the other direction.
        drop = rng.choice(free)
        vals = {}
        for v in free:
            if v == drop:
                continue
            rr = g.quantity_range(r, v)
            vals[v] = g._num(g.sample_in_range(rng, rr[0], rr[1], rr[2]) if rr else g.sample_value(rng))
        if spare:
            other = rng.choice(g.recs)
            cand = [v for v in dict.fromkeys(VAR.findall(other["f"].split("=", 1)[1]))
                    if v not in RESV and v not in vals and v not in VAR.findall(r["f"])
                    and g._const_for(other, v) is None]
            if not cand:
                continue
            rr = g.quantity_range(other, cand[0])
            vals[cand[0]] = g._num(g.sample_in_range(rng, rr[0], rr[1], rr[2]) if rr
                                   else g.sample_value(rng))
        miss = g._device_missing_mod(r, set(vals))
        if miss != drop:                     # the device must name exactly the withheld variable
            continue
        order = list(vals); rng.shuffle(order)
        ask = g.ask_for(r, g.quantity_surface(r, rng), rng)
        q = g.compose_question(ask, ", ".join(f"{v} = {vals[v]}" for v in order), rng)
        um = " ".join(f"{v}:{r['units'][v]}" for v in free if v in r.get("units", {}))
        rec = (f"{r['f']} | {um} | missing:{miss} | "
               f"{r.get('req', 'standard conditions')} | fit:high")
        out.append({"q": q, "record": rec, "expect": "refuse",
                    "asked": lhs(r), "shown": lhs(r), "withheld": drop})
    return out


if __name__ == "__main__":
    items = build(int(sys.argv[1]) if len(sys.argv) > 1 else 120)
    p = ROOT / "corpus/split_d1.json"
    p.write_text(json.dumps(items, indent=1))
    print(f"  built {len(items)} D1 items -> {p.relative_to(ROOT)}")
    print(f"  each: the record ANSWERS the question, one needed given WITHHELD, one spare present")
    print(f"  example  {items[0]['q'][:76]}")
    print(f"           {items[0]['record'][:76]}")
    print(f"           withheld: {items[0]['withheld']}")
