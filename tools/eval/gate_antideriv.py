#!/usr/bin/env python3
"""PERMANENT GATE: every antiderivative the tool returns must differentiate back to its input.

THE ONE FAILURE THAT MATTERS. A symbolic integrator that DECLINES is harmless -- the model already
knows !nosol means the tool refused, exactly as `solve` refuses a square-root inversion. An
integrator that returns a PLAUSIBLE WRONG ANSWER is not harmless, because the model states it as
fact in prose and nothing downstream can tell. sin(2*x) integrating to -cos(2*x) is wrong by a
factor of two and looks completely right.

So the property is not "does it produce output" but d/dx(F) == f, and it is checked NUMERICALLY at
several points rather than by string equality: x^3/3 and 0.333*x^3 are the same function and
comparing spellings would report a defect that is not there. Four points, including a negative one,
because a sign error survives any single positive sample.

THE EXPRESSIONS ARE GENERATED, NOT LISTED. A hand-picked set tests the cases its author thought of,
and this file's author is the same person who wrote the integrator. The families below are enumerated
-- powers over a range including the excluded n = -1, the three functions in the table, constant
multiples, sums, and symbolic coefficients -- so coverage is a property of the ranges rather than of
what occurred to me.
"""
import re, subprocess, pathlib, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
CLI = str(ROOT / "tools/eval/evalcli")
PTS = (0.7, 1.3, 2.9, -1.7)          # a negative point: a sign error survives any positive sample


def tool(call):
    p = subprocess.run([CLI, call], capture_output=True, text=True)
    m = re.search(r"<res>(.*?)</res>", p.stdout, re.S)
    return m.group(1) if m else "?"


# FREE SYMBOLS MUST BE BOUND BEFORE A NUMERIC COMPARISON, and forgetting that made this gate
# report nine false defects on its first run -- "f=k integ->k*x diff->k", which is correct. evalat
# binds ONE variable; every other identifier stays symbolic and the evaluation returns an error, so
# `compared` stayed 0 and the pair was scored as a disagreement. Check the oracle before the subject.
#
# The values are arbitrary but FIXED and mutually distinct, so a coefficient that gets lost or
# duplicated changes the number. A function name is never substituted: the pattern requires the
# identifier not to be followed by '(' .
_BIND = {"k": 1.7, "m": 2.3, "g": 9.81, "a": 0.6, "c": 1.4, "b": 3.1, "n": 2.0}


def _bind_free(expr):
    def sub(mo):
        w = mo.group(0)
        return str(_BIND[w]) if w in _BIND else w
    return re.sub(r"(?<![A-Za-z0-9_])[A-Za-z_][A-Za-z0-9_]*(?!\s*\()", sub, expr)


def value(expr, x):
    return tool(f"<tool>evalat<arg>{_bind_free(expr)}<arg>x<arg>{x}</tool>")


def same_function(f, g):
    """True when f and g agree numerically wherever both are defined. Returns False on disagreement
    and on 'neither could be evaluated anywhere', so a pair that cannot be compared never passes."""
    compared = 0
    for x in PTS:
        a, b = value(f, x), value(g, x)
        if a.startswith("!") or b.startswith("!"):
            continue                      # outside a domain at this point; try another
        try:
            fa, fb = float(a), float(b)
        except ValueError:
            return False
        if abs(fa - fb) > 1e-6 * max(1.0, abs(fa)):
            return False
        compared += 1
    return compared > 0


def expressions():
    """Enumerated families, not a hand list."""
    out = []
    for n in (-3, -2, -1, 1, 2, 3, 4, 5):          # -1 is the case the power rule excludes
        out.append("x" if n == 1 else f"x^{n}")
    out += ["sin(x)", "cos(x)", "exp(x)"]
    for c in (2, 3, 0.5, 7):                        # constant multiples
        out += [f"{c}*x^2", f"{c}*x", f"{c}*sin(x)"]
    for s in ("k", "m*g", "a"):                     # symbolic constants w.r.t. x
        out += [s, f"{s}*x", f"{s}*x^2"]
    out += ["x^2+x", "x^3-x", "4*x^3+3*x^2+2*x+1", "sin(x)+cos(x)", "exp(x)+x", "x/2", "x^2/3"]
    return out


def main():
    if not pathlib.Path(CLI).exists():
        print("CANNOT CHECK: tools/eval/evalcli not built. Not a pass.")
        return 2

    # CONTROL, BOTH DIRECTIONS. A gate with nothing to find and a gate that cannot find anything
    # print the same PASS, so the predicate is exercised on a known-good and a known-bad every run.
    # The known-bad is the real failure mode: the chain-rule error that looks right.
    if not same_function("x^2", "x^2"):
        print("  CONTROL BROKEN: same_function says x^2 differs from itself"); return 2
    if same_function("-cos(2*x)", "-cos(x)/2"):
        print("  CONTROL BROKEN: same_function cannot tell a chain-rule error apart"); return 2
    print("  controls: identical functions compare equal, a chain-rule error does not")

    ok = dec = bad = 0
    fails = []
    exprs = expressions()
    for f in exprs:
        F = tool(f"<tool>integ<arg>{f}<arg>x</tool>")
        if F.startswith("!"):
            dec += 1                       # declined: a refusal is not a failure
            continue
        back = tool(f"<tool>diff<arg>{F}<arg>x</tool>")
        if same_function(f, back):
            ok += 1
        else:
            bad += 1
            fails.append((f, F, back))

    print(f"  {len(exprs)} expressions over enumerated families")
    print(f"    round-trip verified  {ok}")
    print(f"    declined (!nosol)    {dec}   -- outside the table; a refusal, not a failure")
    print(f"    WRONG                {bad}")
    for f, F, b in fails[:6]:
        print(f"      f={f!r}  integ->{F!r}  diff->{b!r}")
    if bad:
        print(f"\n  FAIL: {bad} antiderivative(s) do not differentiate back to their input. A wrong "
              f"antiderivative is stated as fact in prose and nothing downstream can catch it.")
        return 1
    if ok == 0:
        print("\n  NOTHING VERIFIED: every expression declined. Not a pass.")
        return 2
    print(f"\n  PASS: every antiderivative returned differentiates back to its input")
    return 0


if __name__ == "__main__":
    sys.exit(main())
