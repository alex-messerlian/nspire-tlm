#!/usr/bin/env python3
"""C3 definite integrals: the properties a structural check cannot see.

C3 exists because TOOL_SPEC declares `integ` at arity 4 and the corpus trained it ZERO times. The
first version of the tier swept every (record, variable) pair and produced integrals of a period
with respect to frequency and `integral of theta_i dtheta_i` attached to the law of reflection --
every one well-formed, executing, and arithmetically correct. That is the A6 wrong-supervision
class, and no gate in this repo could see it. This one asserts the three properties that were wrong
when read by hand, so each is a check a reader would otherwise have to repeat:

  1. THE FREE-FALL INTERVAL IS INSIDE THE FLIGHT TIME. A body thrown up at v_0 is in the air from 0
     to 2*v_0/g, and the relation describes nothing after impact. A fixed window integrated one
     throw from 7.76 s to 11.91 s when it landed at 3.49 s. The interval is COUPLED to the givens
     and a constant window cannot express it.
  2. EVERY LITERAL IN THE CALL IS IN THE QUESTION. Arity-4 integ rejects free symbols, so the other
     givens are bound to numbers; if a bound number is not stated, the model is being taught to
     emit a value it cannot read -- the A19 defect, which was indistinguishable from correct to
     every grader in the repo.
  3. THE ANSWER ROUNDS AND <res> DOES NOT. RESULT_RES_SPAN.md: the answer restated the result span
     byte-for-byte in 100% of documents, so the corpus never demonstrated a rounding step.
"""
import math, pathlib, random, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "corpus"))

NUM = re.compile(r"-?\d+(?:\.\d+)?(?:e[+-]?\d+)?")


def sample(dose=10, seed=5, window_override=None):
    import generate as G, calculus as CA
    rng = random.Random(seed)

    def draw(rec, var):
        w = G.quantity_range(rec, var)
        if not w:
            return None
        lo, hi, integral, _ = w
        return G.sample_in_range(rng, lo, hi, integral)

    if window_override is not None:
        CA.ACCUM[("v=v_0-g*t", "t")]["window"] = window_override
    return CA.build_definite(G.recs, G._run_tool_text, rng, draw, dose=dose)[0]


def flight_violations(docs):
    bad = []
    for d in docs:
        if d["head"] != "v=v_0-g*t":
            continue
        v0 = re.search(r"v_0 = ([\d.e+-]+)", d["q"])
        g = re.search(r"\bg = ([\d.e+-]+)", d["q"])
        if not (v0 and g):
            bad.append((d["q"][:60], "givens not stated"))
            continue
        hi = float(d["call"].split("<arg>")[-1].replace("</tool>", ""))
        t_land = 2 * float(v0.group(1)) / float(g.group(1))
        if hi > t_land + 1e-9:
            bad.append((d["q"][:60], f"integrates to {hi:.3f}s, lands at {t_land:.3f}s"))
    return bad


def unstated_literals(docs):
    bad = []
    for d in docs:
        inner = d["call"].split("<arg>", 1)[1].replace("</tool>", "")
        qn = set(NUM.findall(d["q"]))
        for lit in NUM.findall(inner):
            if lit not in qn and lit.lstrip("-") not in qn:
                bad.append((d["q"][:60], lit))
    return bad


def rounding(docs):
    """The answer must state the 4 s.f. value, and must differ from <res> where <res> is longer."""
    wrong, demonstrated = [], 0
    for d in docs:
        want = f"{float(d['res']):.4g}"
        if want not in d["ans"]:
            wrong.append((d["res"], d["ans"][:60]))
        elif want != d["res"]:
            demonstrated += 1
    return wrong, demonstrated


def main():
    docs = sample()
    if not docs:
        print("  NOTHING TO CHECK: the C3 builder produced no documents. Not a pass.")
        return 2
    print(f"  {len(docs)} C3 documents built over "
          f"{len({d['head'] for d in docs})} declared accumulations")

    # POSITIVE CONTROL: a CONSTANT window is the defect this gate was written for, and it must fail.
    ctl = flight_violations(sample(window_override=(0.0, 12.0), seed=9))
    if not ctl:
        print("  CONTROL BROKEN: a constant (0,12) free-fall window produced no violation, so the "
              "flight-time check cannot fire. That window is the shipped defect.")
        return 2
    print(f"  control: a constant free-fall window violates the flight time {len(ctl)} times")

    fv = flight_violations(docs)
    ul = unstated_literals(docs)
    rw, demo = rounding(docs)
    print(f"  free-fall intervals past landing : {len(fv)}")
    print(f"  call literals absent from the question: {len(ul)}")
    print(f"  answers not stating the 4 s.f. value  : {len(rw)}   "
          f"(rounding demonstrated on {demo} of {len(docs)})")
    if fv or ul or rw:
        for q, why in (fv + ul + rw)[:5]:
            print(f"    {q}  <-  {why}")
        print("\n  FAIL")
        return 1
    if demo == 0:
        print("\n  FAIL: no document rounds, so the corpus never demonstrates the step "
              "(RESULT_RES_SPAN.md).")
        return 1
    print("\n  PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
