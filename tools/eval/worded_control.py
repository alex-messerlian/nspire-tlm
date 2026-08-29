#!/usr/bin/env python3
"""THE INVERTED CONTROL for lexical symbol-matching.

`fit` rose 6.4% -> 64.7% and the magnitude is the one that was a surface rule last time (fit_m at
98.9%). The remaining suspect after the property sweep found nothing at scale is the shallowest
possible reading of the task:

    the asked quantity's SYMBOL is not the record's LHS  ->  refuse

which is right on every `fit` item and requires no understanding of what the relation means.

The control inverts it, the way answer_x inverted the spare-given rule: build ANSWERABLE items in
which the record's LHS symbol NEVER appears in the question -- the quantity is named in words only.
The cue then says "refuse" and the correct answer is "answer". A model matching symbols refuses
these; a model reading the relation answers them.

Its partner is `answer_s`, the same items with the SYMBOL surface, where cue and truth agree. The
pair isolates exactly one variable: whether the asked quantity is named by symbol or in words.
"""
import contextlib, importlib.util, io, json, pathlib, random, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "corpus"))
VAR = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
RESV = {"pi", "e", "sin", "cos", "tan", "asin", "acos", "atan", "sqrt", "log", "ln", "exp", "abs"}


def build(n=120, seed=17, worded=True):
    spec = importlib.util.spec_from_file_location("gen", ROOT / "corpus/generate.py")
    g = importlib.util.module_from_spec(spec)
    with contextlib.redirect_stdout(io.StringIO()):
        spec.loader.exec_module(g)
    rng = random.Random(seed)
    lhs = lambda r: r["f"].split("=", 1)[0].strip()
    out = []
    for _ in range(n * 60):
        if len(out) >= n:
            break
        r = rng.choice(g.recs)
        free = [v for v in dict.fromkeys(VAR.findall(r["f"].split("=", 1)[1]))
                if v not in RESV and v != lhs(r) and g._const_for(r, v) is None]
        if not free or len(free) > 3:
            continue
        vals = {}
        for v in free:
            rr = g.quantity_range(r, v)
            vals[v] = g._num(g.sample_in_range(rng, rr[0], rr[1], rr[2]) if rr else g.sample_value(rng))
        if g._device_missing_mod(r, set(vals)) != "none":
            continue
        # DRAW A SURFACE OF THE REQUIRED KIND. quantity_surface returns the canonical name, a head
        # noun, a bare head, or the SYMBOL; take samples until one matches what this arm needs.
        surf = None
        for _try in range(40):
            cand = g.quantity_surface(r, rng)
            is_sym = cand.strip() == lhs(r)
            if is_sym != worded:
                surf = cand; break
        if surf is None:
            continue
        ask = g.ask_for(r, surf, rng)
        order = list(vals); rng.shuffle(order)
        q = g.compose_question(ask, ", ".join(f"{v} = {vals[v]}" for v in order), rng)
        # THE INVARIANT THIS ARM RESTS ON, asserted per item rather than assumed.
        if worded and re.search(re.escape(lhs(r)), q):
            continue                      # the symbol leaked in; not a worded item
        if not worded and lhs(r) not in q:
            continue
        um = " ".join(f"{v}:{r['units'][v]}" for v in free if v in r.get("units", {}))
        out.append({"q": q,
                    "record": f"{r['f']} | {um} | missing:none | "
                              f"{r.get('req', 'standard conditions')} | fit:high",
                    "expect": "answer", "asked": lhs(r), "shown": lhs(r)})
    return out


if __name__ == "__main__":
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 120
    for wd, out in ((True, "corpus/split_answer_w.json"), (False, "corpus/split_answer_s.json")):
        items = build(n=n, worded=wd)
        (ROOT / out).write_text(json.dumps(items, indent=1))
        sym = sum(1 for i in items if re.search(re.escape(i["asked"]), i["q"]))
        print(f"  {'worded' if wd else 'symbol':7s} n={len(items):4d}  "
              f"questions containing the record's LHS symbol: {sym}  -> {out}")
        print(f"           {items[0]['q'][:78]}")
        print(f"           {items[0]['record'][:78]}")
