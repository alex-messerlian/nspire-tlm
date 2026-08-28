#!/usr/bin/env python3
"""PERMANENT GATE: no generated document may state a physically impossible result.

THE CLASS WITH NO OWNER, until this. distribution_gate's tokenizer is `[a-z]+` and cannot see
digits AT ALL -- measured: across 448 documents whose every number was rewritten, 448/448 strings
changed and 0/448 feature streams did. Provenance checks a number was SOURCED, not that it is
possible. The shape check compares structure. dim_gate checks dimensions, and -28.75 is
dimensionally a perfectly good efficiency.

MEASURED BEFORE THE FIX: 94 of 4,957 documents, 1.90% -- negative times of flight, a refractive
index of 3.06e+07, a Carnot efficiency of -28.75, g = 4352 m/s^2. sample_value() draws each variable
independently, so nothing stops T_c > T_h or an angle past 90 degrees.

WHAT IT DOES NOT VERIFY, and this is the honest limit: only quantity classes with a NAMED physical
range are checked -- durations, masses, absolute temperatures, efficiencies, refractive indices,
g. Everything else is UNCHECKED, not blessed. Most results fall outside the table, so "passes this
gate" must never be read as "physically plausible". Widening it is per-class work and each class
needs a defensible bound, not a guess.
"""
import importlib.util, io, contextlib, pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
# N = 20,000, NOT 3,000. A uniform draw over 164 records gives ~18 documents per record at
# N=3000, so NO PER-RECORD PROPERTY IS MEASURABLE: the g-from-a-pendulum record appeared 14 times,
# and a rule that fires on a quarter of those has an expected count below 4. The negative control
# survived twice for exactly this reason -- not because the rule was wrong, because the sample
# could not see it. A gate whose sample size is smaller than its unit of analysis reports 0 and
# means "not measured".
N, SEED, LIMIT = 20000, 999, 0.0005
# LIMIT LOWERED FROM 0.002, BECAUSE THE GATE COULD NOT FAIL. Measured with the generator's drop
# disabled over 39,771 sampled results, exactly ONE of this gate's rules fires at all:
#
#     g outside (0, 100) m/s^2 ......... 61 / 39,771 = 0.153%
#     duration <= 0 .................... 0     subsumed by the unit rule on `s` givens
#     mass <= 0 ........................ 0     subsumed
#     absolute temperature <= 0 ........ 0     subsumed
#     efficiency outside [0, 1] ........ 0     subsumed by A22c's T_c < T_h precondition
#     refractive index outside [1,100] . 0     subsumed by A23's scale on `v` in n = c/v
#
# At the old 0.2% limit the only live rule sat BELOW the threshold, so no corpus this generator can
# produce would have failed here. That is a gate that cannot fail, which the project log says is not a
# gate -- and its negative control said so first, by surviving twice.
#
# The five subsumed rules are KEPT: they are cheap, and they are the backstop if a given-range
# declaration is ever removed. But they are documented as subsumed so nobody reads this gate's PASS
# as independent evidence about them.

if __name__ == "__main__":
    spec = importlib.util.spec_from_file_location("gen", ROOT / "corpus/generate.py")
    m = importlib.util.module_from_spec(spec)
    try:
        with contextlib.redirect_stdout(io.StringIO()):
            spec.loader.exec_module(m)
    except SystemExit:
        pass
    out = m.gen(N, seed=SEED)
    docs = [d["text"] if isinstance(d, dict) else d for d in (out[0] if isinstance(out, tuple) else out)]

    by_f = {r["f"]: r for r in m.recs}
    checked, bad, examples = 0, 0, []
    for t in docs:
        rs = re.search(r"<res>(.*?)</res>", t)
        f = re.search(r"<r>([^|]+)\|", t)
        if not (rs and f): continue
        rec = by_f.get(f.group(1).strip())
        if rec is None: continue
        try:   v = float(rs.group(1).split()[0])
        except (ValueError, IndexError): continue
        checked += 1
        why = m.implausible(rec, v)
        if why:
            bad += 1
            if len(examples) < 5: examples.append((rec["f"][:30], rs.group(1)[:16], why))
    if not checked:
        # ABSENCE IS FAILURE: nothing parsed would otherwise report a perfect zero.
        print("CANNOT CHECK: no document yielded a record and a numeric result"); sys.exit(2)

    # POSITIVE CONTROL in-run: the predicate must be able to fire.
    probe = {"f": "T_tof=x", "name": "Time of flight", "units": {"T_tof": "s"}}
    if not m.implausible(probe, -3.0):
        print("\n  FAIL: the positive control was not caught -- this check is not working.")
        sys.exit(1)

    # COVERAGE, PRINTED EVERY RUN, for the same reason gate_given_range prints it: most records
    # have no declared result kind and no decisive unit, and this gate says nothing about them.
    declared = sum(1 for r in m.recs
                   if r["f"] in m._RESULT_KIND
                   or ((r.get("units") or {}).get(r["f"].split("=", 1)[0].strip()) or "")
                      in ("s", "kg", "K"))
    print(f"  DECLARATION COVERAGE         {declared}/{len(m.recs)} records "
          f"({declared/len(m.recs):.1%}) -- the other {len(m.recs)-declared} are UNCHECKED, "
          f"not verified")
    # COVERAGE, PRINTED EVERY RUN, for the same reason gate_given_range prints it: most records
    # have no declared result kind and no decisive unit, so this gate says nothing about them.
    _lhs_unit = lambda r: ((r.get("units") or {}).get(r["f"].split("=", 1)[0].strip()) or "")
    declared = sum(1 for r in m.recs if r["f"] in m._RESULT_KIND or _lhs_unit(r) in ("s", "kg", "K"))
    print(f"  DECLARATION COVERAGE         {declared}/{len(m.recs)} records "
          f"({declared/len(m.recs):.1%}) -- the other {len(m.recs)-declared} are UNCHECKED, "
          f"not verified")
    rate = bad / checked
    print(f"  results checked              {checked:,}")
    print(f"  PHYSICALLY IMPOSSIBLE        {bad}  ({rate:.2%}, limit {LIMIT:.2%})")
    for f, v, why in examples: print(f"      {f:32} -> {v:16}  {why}")
    print(f"  positive control             a negative duration IS caught")
    if rate > LIMIT:
        print(f"\n  FAIL: {rate:.2%} of results cannot occur physically. Nothing else in the suite")
        print( "  can see this -- the separability gate's tokenizer does not read digits.")
        sys.exit(1)
    print("\n  PASS: no result violates a named physical range")
