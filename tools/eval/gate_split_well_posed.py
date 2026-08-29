#!/usr/bin/env python3
"""A split item's question must ask for the quantity its record computes.

corpus/build_splits.py paired a SHUFFLED OpenStax sentence with a record by `i % len(st)` and
appended the record's values, so question and record were unrelated by construction:

    Q "What is the cost savings for using the LED in place of the incandescent bulb for one year..."
    R v_CM=r*omega | omega:1/s r:m | missing:none | ... | fit:high        expect: ANSWER

Retrain 2 refuses 94.6% of those, which is correct, and the arm scored it as failure -- so the model
that improved scored worse, and the SELECT answer metric had never measured record-reading. Every
number it produced (58.8%, 47.5%, 20.0%, 3.6%) was on that shape.

Two invariants, both source-side and needing no model:
  1. an `expect: answer` item names its record's LHS -- by symbol, or by the record's own name
  2. an `expect: answer` item is FULLY BOUND (`missing:none`); 3 of 37 carried missing:<var> and
     were still labelled answerable, which is a refusal item wearing the wrong label
"""
import json, pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
SPLITS = ("corpus/split_select.json", "corpus/split_report.json")


def head_words(name):
    return {w for w in re.findall(r"[A-Za-z]+", (name or "").lower()) if len(w) > 3}


def main():
    import contextlib, importlib.util, io
    spec = importlib.util.spec_from_file_location("gen", ROOT / "corpus/generate.py")
    g = importlib.util.module_from_spec(spec)
    with contextlib.redirect_stdout(io.StringIO()):
        spec.loader.exec_module(g)
    byf = {r["f"]: r for r in g.recs}
    for r in json.loads((ROOT / "corpus/units_holdout.json").read_text()):
        byf.setdefault(r["f"], r)

    bad_ask, bad_bound, n = [], [], 0
    for sp in SPLITS:
        f = ROOT / sp
        if not f.exists():
            print(f"CANNOT CHECK: {sp} absent -- not a pass"); return 2
        for it in json.loads(f.read_text()):
            if it["expect"] != "answer":
                continue
            n += 1
            rec = it["record"]
            lhs = rec.split("|", 1)[0].split("=", 1)[0].strip()
            q = it["q"]
            # \b DOES NOT WORK ON A SYMBOL ENDING IN A NON-WORD CHARACTER. `U(x)` ends in ')',
            # so \bU\(x\)\b can never match and the gate flagged "estimate U(x)." as not naming
            # U(x). Word boundaries only where the symbol is word-characters throughout.
            pat = (rf"\b{re.escape(lhs)}\b" if re.fullmatch(r"\w+", lhs)
                   else re.escape(lhs))
            named = re.search(pat, q) is not None
            if not named:
                hw = head_words((byf.get(rec.split("|", 1)[0].strip(), {}) or {}).get("name"))
                named = bool(hw & head_words(q))
            if not named:
                bad_ask.append((it.get("id", "?"), q[:70], lhs))
            if " missing:none " not in rec:
                bad_bound.append((it.get("id", "?"), rec[:70]))

    print(f"  answerable split items: {n}")
    print(f"  questions not naming the record's quantity: {len(bad_ask)}")
    for i, q, l in bad_ask[:3]:
        print(f"    {i}: asks nothing about '{l}' -- {q}")
    print(f"  answerable items that are NOT fully bound: {len(bad_bound)}")
    for i, r in bad_bound[:3]:
        print(f"    {i}: {r}")
    if bad_ask or bad_bound:
        print("FAIL: a split item the model is scored for answering must ask for what its record "
              "computes, and must supply what that record needs.")
        return 1
    print("PASS: every answerable split item asks for its record's quantity and is fully bound")
    return 0


if __name__ == "__main__":
    sys.exit(main())
