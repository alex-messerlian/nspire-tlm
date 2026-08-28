#!/usr/bin/env python3
"""PERMANENT GATE: a declared range must not collapse its variable to a single value.

A range is meant to bound a DRAW. `sample_in_range` preferred the mined pool whenever the window
contained ANY pool value, so a window containing exactly ONE collapsed the variable: seven declared
givens -- q on three records, Q, sigma, and a capacitor plate gap -- were drawn as **0.001 in 100%
of documents**.

**A variable that never varies is worse than one drawn too wide**, and no range check can see it:
the value is inside its window every single time. The corpus teaches a constant where the physics
has a free parameter, and every gate agrees.

WHAT IT DOES NOT VERIFY: that the range is CORRECT, or that the distribution within it is sensible.
It verifies only that more than one value is reachable. A window admitting two values passes here
and is still nearly a constant -- the threshold is deliberately low so this gate stays about the
collapse, and the reviewer's separate finding (48 windows that admit the whole pool and so exclude
nothing) is a different property that is reported by gate_given_range's coverage line.
"""
import importlib.util, io, contextlib, pathlib, random, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
DRAWS = 40

if __name__ == "__main__":
    spec = importlib.util.spec_from_file_location("gen", ROOT / "corpus/generate.py")
    m = importlib.util.module_from_spec(spec)
    try:
        with contextlib.redirect_stdout(io.StringIO()):
            spec.loader.exec_module(m)
    except SystemExit:
        pass

    rng = random.Random(0)
    checked, bad = 0, []
    for r in m.recs:
        for v in sorted({x for x in m.VAR.findall(r["f"].split("=", 1)[1])} - {"pi", "e"}):
            if m._const_for(r, v) is not None:
                continue                       # a constant is SUPPOSED to be one value
            rr = m.quantity_range(r, v)
            if rr is None:
                continue                       # undeclared: gate_given_range counts those
            checked += 1
            drawn = {m.sample_in_range(rng, rr[0], rr[1], rr[2]) for _ in range(DRAWS)}
            if len(drawn) == 1:
                bad.append((r["f"], v, drawn.pop()))
    if not checked:
        # ABSENCE IS FAILURE: no declared pair would otherwise report a clean run.
        print("CANNOT CHECK: no declared (record, variable) pair"); sys.exit(2)

    # POSITIVE CONTROL in-run, on a DEGENERATE window, which must collapse whatever the sampler
    # does. The first version probed a window containing one POOL value -- and that no longer
    # collapses, because the fix routes such a window to the log-uniform draw. It was testing the
    # behaviour being repaired rather than the detector, so it failed the moment the repair landed.
    # lo == hi can only ever yield one value, so this exercises the detection and nothing else.
    probe = {m.sample_in_range(rng, 5.0, 5.0, False) for _ in range(DRAWS)}
    if len(probe) != 1:
        print("\n  FAIL: a degenerate window did not collapse -- the detector is not working.")
        sys.exit(1)
    print(f"  positive control             a degenerate window IS detected")

    print(f"  declared pairs checked       {checked}")
    print(f"  COLLAPSED TO ONE VALUE       {len(bad)}")
    for f, v, val in bad[:8]:
        print(f"      {f[:36]:38} {v:12} always {val}")
    if bad:
        print(f"\n  FAIL: {len(bad)} declared given(s) take the same value in every document. The")
        print( "  model would see a constant where the physics has a free parameter, and no range")
        print( "  check can see it -- the value is inside its window every time.")
        sys.exit(1)
    print("\n  PASS: every declared given can take more than one value")
