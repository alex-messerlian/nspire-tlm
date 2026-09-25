#!/usr/bin/env python3
"""Check score_correct's REFERENCES against an evaluator that shares no code with the device's.

    .venv-tok/bin/python tools/eval/independent_refs.py

score_correct.py computes each item's reference with evalcli -- the same evaluator the corpus
generator and the calculator use. That removes a second implementation of the maths from the
SCORER, and it also means a defect shared by the evaluator and the references would pass unseen.
This recomputes every reference in Python's `math`, from the
same substituted expression, and reports agreement. Only syntax is translated (`^` -> `**`,
implicit multiplication `)(` and `2(` -> `*`, `|x|` -> abs, `ln`/`log` to natural/base-10), and every item that cannot
be translated is COUNTED, not skipped silently.

Agreement tolerance is 1e-8 relative: evalcli prints ten significant figures.
"""
import json, math, pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/eval"))
import score_correct as S                                     # noqa: E402

ENV = {"sin": math.sin, "cos": math.cos, "tan": math.tan, "asin": math.asin, "acos": math.acos,
       "atan": math.atan, "sqrt": math.sqrt, "exp": math.exp, "ln": math.log, "log": math.log10,
       "abs": abs, "pi": math.pi, "e": math.e, "__builtins__": {}}


def py_eval(expr):
    src = expr.replace("^", "**")
    src = re.sub(r"\|([^|]*)\|", r"abs(\1)", src)             # |x| -> abs(x); bars do not nest here
    src = re.sub(r"\)\s*\(", ")*(", src)                       # (a)(b) -> (a)*(b)
    src = re.sub(r"(?<![A-Za-z_])(\d)\s*\(", r"\1*(", src)     # 2(x) -> 2*(x), not sqrt2(...)
    return float(eval(src, ENV))                              # noqa: S307 (fixed, local inputs)


def main():
    total = agree = untranslatable = 0
    worst = []
    for arm in S.ARMS:
        sc, _ = S.load(arm)
        for it, ref in sc:
            total += 1
            expr = S.substituted(it)
            try:
                v = py_eval(expr)
            except Exception as exc:                          # noqa: BLE001
                untranslatable += 1
                worst.append((float("inf"), arm, expr, f"untranslatable: {exc}"))
                continue
            rel = abs(v - ref) / max(abs(ref), 1e-300)
            agree += rel <= 1e-8
            worst.append((rel, arm, expr, f"python {v!r} evalcli {ref!r}"))
    worst.sort(reverse=True)
    print(f"references checked: {total}; agree within 1e-8: {agree}; untranslatable: {untranslatable}")
    for rel, arm, expr, note in worst[:5]:
        print(f"   {arm:8s} rel {rel:.2e}  {expr[:60]}  ({note})")
    return 0 if agree == total else 1


if __name__ == "__main__":
    sys.exit(main())
