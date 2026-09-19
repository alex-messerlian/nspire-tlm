#!/usr/bin/env python3
"""C1/C2: differentiation and integration of the store's own relations, through `diff` and `integ`.

WHY THE RECORD IS A PHYSICS RELATION AND NOT "x^2". The scope is the math that is PART OF physics,
so a calculus question here asks how one physical quantity changes with another, and the record it
is asked against is the relation between them. "What is the derivative of x^2" is arithmetic; "how
does kinetic energy change with speed" is physics, and the answer is momentum.

MEASURED before building: 398 of 405 (record, variable) pairs differentiate to a clean expression,
5 render badly (nested powers) and 2 decline. So the surface is real, and the 7 are skipped and
counted rather than papered over.

THREE OF THE LINKS ARE THE POINT, and they were found by differentiating every record and seeing
which results are ANOTHER RECORD in the store:

    d/dt (d_0 + v_0*t + a*t^2/2)  =  v_0 + a*t     velocity IS the derivative of position
    d/dv (0.5*m*v^2)              =  m*v           momentum IS the derivative of kinetic energy
    d/dh (m*g*h)                  =  m*g           force IS the gradient of potential energy

Those get answers that name the relation. The rest get answers that state the rate of change, which
is what the question asked and is true whether or not the result is separately famous.

INTEGRATION IS DELIBERATELY THINNER. `integ` is table-driven and narrow by design (A98), so most
store relations decline, and a document whose <res> is !nosol teaches the model to ask for what it
cannot have. C2 is generated only where the tool actually returns an antiderivative.
"""
import os, re

DOSE = int(os.environ.get("C1_DOSE", "24"))

# C3 IS DOSED SEPARATELY BECAUSE IT IS SIZED BY ACCUMULATIONS, NOT BY PAIRS. C1 runs over ~380
# (record, variable) pairs at 24 each; C3 has FIVE declared accumulations, so the same dose gives 60
# documents against C1's 9,168 -- a tier too thin to learn. The dose is per accumulation, and one
# fixed explanatory clause repeated across them is deliberate: A82 measured that one explanation
# seen often beats three seen rarely, and the numbers vary while the reading does not.
C3_DOSE = int(os.environ.get("C3_DOSE", "400"))

# Results that render badly. 5 of 405, all nested powers. Skipped for the same reason the ugly
# rearrangements were fixed rather than shipped: these become training data, and a model taught
# from "E_0*2*n/(n^2)^2" has learned to write that.
UGLY = re.compile(r"\d\*\d|\(1/|\^\d+\)\^")

D_FRAMES = [
    "what is the derivative of {f} with respect to {v}",
    "differentiate {f} with respect to {v}",
    "d{lhs}/d{v} for {f}",
    "how does {lhs} change with {v} in {f}",
    "rate of change of {lhs} with {v}, given {f}",
    "{f} -- differentiate for {v}",
    "take the derivative of {f} in {v}",
    "what is d{lhs}/d{v} when {f}",
    "find the derivative of {f} for {v}",
    "how fast does {lhs} change as {v} changes, if {f}",
]

D_PROSE = [
    "d{lhs}/d{v} = {r}. That is how fast {lhs} changes when {v} does.",
    "Differentiating gives d{lhs}/d{v} = {r}.",
    "{r}. That is the rate of change of {lhs} with {v}.",
    "d{lhs}/d{v} = {r}, so a small change in {v} moves {lhs} by that much times the change.",
    "The derivative is {r}. Hold everything else fixed and that is the slope.",
    "d{lhs}/d{v} works out to {r}.",
    "{r} -- the slope of {lhs} against {v}.",
    "Take {v} as the variable and the rest as constants: d{lhs}/d{v} = {r}.",
]

# The three results that ARE another record. Written out, because "m*v" is a correct answer and
# "momentum" is the one a student remembers.
NOTABLE = {
    # NAMES THE PHYSICS, DOES NOT STATE A SECOND RELATION. The first version said "which is the
    # velocity relation v = v_0 + a*t", and gate_name_provenance fired on it -- correctly. That
    # gate exists because a model asserting a record name it cannot read from the prompt supplies a
    # DIFFERENT record's name 89.7% of the time on anything it has not memorised, and teaching the
    # habit on three records it has memorised is still teaching the habit. The physics survives
    # without the second formula.
    ("d=d_0+v_0*t+((1)/(2))*a*(t)^(2)", "t"):
        "d{lhs}/d{v} = {r}. Differentiate a position with respect to time and you get velocity, "
        "and that is not a coincidence: it is what velocity means.",
    ("K=0.5*m*(v)^(2)", "v"):
        "d{lhs}/d{v} = {r}, which is the momentum of the same body. Differentiating an energy "
        "with respect to speed gives a momentum, and that is why the two move together.",
    ("U=m*g*h", "h"):
        "d{lhs}/d{v} = {r}, which is the weight. Differentiating a potential energy with respect "
        "to height gives back the force that stored it.",
}

I_FRAMES = [
    "what is the integral of {rhs} with respect to {v}",
    "integrate {rhs} over {v}",
    "find the antiderivative of {rhs} in {v}",
    "{rhs} -- integrate with respect to {v}",
    "what integrates to give {rhs} in {v}",
]

I_PROSE = [
    "The integral is {r}.",
    "Integrating {rhs} over {v} gives {r}.",
    "{r}, up to a constant. Differentiating it back returns {rhs}.",
    "Antidifferentiating gives {r}.",
    "{r} -- and the constant of integration is whatever the starting condition sets.",
]


def pairs(recs):
    FN = {"pi", "e", "sin", "cos", "tan", "sqrt", "exp", "ln", "log", "asin", "acos", "atan"}
    out = []
    for r in recs:
        f = r.get("f") or ""
        if "=" not in f:
            continue
        lhs, rhs = f.split("=", 1)
        lhs = lhs.strip()
        decl = r.get("units") or {}
        for v in dict.fromkeys(x for x in re.findall(r"[A-Za-z_][A-Za-z0-9_]*", rhs)
                               if x not in FN):
            if v in decl and v != lhs:
                out.append((r, lhs, rhs, v))
    return out


def build(recs, run_tool, rng, dose=DOSE):
    """Return (documents, skipped). `run_tool` takes a full <tool>...</tool> and returns <res>."""
    docs, skipped = [], []
    for r, lhs, rhs, v in pairs(recs):
        f = r["f"]
        # ---- C1 derivative -----------------------------------------------------------------
        call = f"<tool>diff<arg>{rhs}<arg>{v}</tool>"
        res = run_tool(call)
        if not res or res.startswith("!") or UGLY.search(res):
            skipped.append((f, v, "diff", res or "?"))
        else:
            note = NOTABLE.get((f, v))
            for i in range(dose):
                q = rng.choice(D_FRAMES).format(f=f, v=v, lhs=lhs)
                # The notable answer is used on a THIRD of that pair's documents, not all of them:
                # a record whose every answer names the same famous relation teaches the name, and
                # the question asked for the derivative.
                tpl = note if (note and i % 3 == 0) else rng.choice(D_PROSE)
                docs.append({"head": f, "kind": "C1", "q": q, "call": call, "res": res,
                             "ans": tpl.format(lhs=lhs, v=v, r=res)})
        # ---- C2 indefinite integral, only where the tool actually returns one ----------------
        icall = f"<tool>integ<arg>{rhs}<arg>{v}</tool>"
        ires = run_tool(icall)
        if not ires or ires.startswith("!") or UGLY.search(ires):
            skipped.append((f, v, "integ", ires or "?"))
            continue
        for _ in range(max(1, dose // 3)):
            q = rng.choice(I_FRAMES).format(rhs=rhs, v=v)
            docs.append({"head": f, "kind": "C2", "q": q, "call": icall, "res": ires,
                         "ans": rng.choice(I_PROSE).format(r=ires, rhs=rhs, v=v)})
    return docs, skipped



# ---- C3: DEFINITE INTEGRALS ----------------------------------------------------------------
# MEASURED GAP. TOOL_SPEC v1.3.0 declares `integ` at arity 2 (indefinite, symbolic) AND arity 4
# (definite, numeric). The runtime computes the second correctly -- integ(x^2,x,0,3)=9,
# integ(sin(x),x,0,pi)=2, integ(1/x,x,1,2)=0.6931471806 -- and the corpus trained it ZERO times.
# A capability the runtime has and the corpus never demonstrates is one the model will not use.
#
# THE FIRST VERSION SWEPT EVERY (record, variable) PAIR AND WAS PHYSICAL NONSENSE. It generated
# integrals of a period with respect to frequency, of an RMS voltage over peak voltage, and
# `integral of theta_i dtheta_i` attached to the law of reflection. Every one was well-formed, every
# call executed, every result was arithmetically right, and not one was a question about physics --
# the A6 ill-posed class exactly, and against this module's own stated principle: the derivative of
# x^2 is arithmetic, how kinetic energy changes with speed is physics. It was caught by reading
# twelve documents, which no structural check would have flagged.
#
# A DEFINITE INTEGRAL IS PHYSICS WHEN THE INTEGRAND IS A RATE AND THE VARIABLE IS WHAT IT
# ACCUMULATES OVER. That is not derivable from the formula -- it is knowledge about what the
# quantities mean -- so it is DECLARED per (record, variable), with the accumulated quantity named
# and its unit stated. Same discipline as the given-range declarations: a lookup, not a heuristic,
# and absence is countable rather than silently wrong. The table is deliberately short. Five honest
# accumulations beat three hundred that execute.
#
# AND THE OTHER GIVENS ARE BOUND TO NUMBERS, WHICH IS WHY THE SWEEP DECLINED 278 PAIRS. Arity-4
# integ returns !expr on any free symbol other than the integration variable, so `integ(-k*x,x,0,2)`
# fails and `integ(-40*x,x,0,0.2)` returns -0.8. Binding them is not a workaround: it is what a
# physics problem does, and it makes these the multi-step questions the brief asks for. Every
# literal in the call is stated in the question, so provenance holds.

# THE INTERVAL IS DECLARED, NOT DRAWN FROM THE QUANTITY WINDOW. The first version drew both limits
# from the variable's full declared range, and produced a body accelerating at 11.7 m/s^2 for 2,762
# seconds to a displacement of 44,693 km. The range declarations bound a SINGLE given; they say
# nothing about how long a textbook sustains the situation, and an interval is not a value. So each
# accumulation declares the interval over which it remains a physics problem, with the reason.
#
# `cap` bounds the RESULT. MEASURED over 1,122 attempts it fires 0.00% -- it is fully SUBSUMED by
# the interval and the scoped given windows, and is kept only as a backstop for a future window
# widened without thinking. It is documented as subsumed so that a C3 run passing is not read as
# independent evidence that the results are plausible; the interval and gwin are what do that work.
# `degenerate` fires 10.87%, which is a live check. Rates are printed on every run, because a bound
# nobody has watched fire is a claim and not a check (A29).

ACCUM = {
    # (record, variable): what accumulates, its unit, the reading, the interval, |result| cap
    ("F=-k*x", "x"): dict(
        what="the work done by the spring", unit="J",
        why="Integrating a force over the distance it acts through gives work. It is negative "
            "because the spring pulls back against the stretch.",
        window=(0.0, 0.5), window_why="a spring stretched more than half a metre is past the "
                                      "linear regime Hooke's law describes",
        cap=5.0e3),
    ("v=v_0+a*t", "t"): dict(
        what="the displacement", unit="m",
        why="Integrating a velocity over time gives displacement. That is what velocity means.",
        window=(0.0, 20.0), window_why="a constant acceleration held longer than ~20 s is not a "
                                       "situation the textbook problems describe",
        gwin={"v_0": (0.0, 40.0), "a": (0.5, 10.0)},
        gwin_why="road and laboratory speeds; the record's own window admits 617 m/s and 131 m/s^2, "
                 "which are correct as single givens and not a textbook kinematics problem",
        cap=1.0e4),
    ("v=v_0-g*t", "t"): dict(
        what="the displacement", unit="m",
        why="Integrating the velocity over time gives how far it moved, with the rise and the "
            "fall counted against each other.",
        # THE INTERVAL IS COUPLED TO THE GIVENS AND A CONSTANT WINDOW CANNOT EXPRESS IT. A body
        # thrown up at v_0 is in flight from 0 to 2*v_0/g and the relation stops describing it at
        # impact. A fixed (0, 12) window integrated from 7.76 s to 11.91 s on a throw that landed
        # at 3.49 s -- arithmetically right, and the object had been underground for four seconds.
        # Found by reading, like every other wrong-supervision defect in this repo.
        window=lambda v: (0.0, 2.0 * v["v_0"] / v["g"]),
        window_why="the flight time 2*v_0/g: the relation describes the body only while it is in "
                   "the air",
        gwin={"v_0": (1.0, 60.0), "g": (9.7, 9.83)},
        gwin_why="a throw or a launch, and g at the Earth's surface rather than any body's gravity",
        cap=2.0e3),
    ("v=a*t", "t"): dict(
        what="the distance travelled", unit="m",
        why="Starting from rest, integrating a*t over time gives the distance covered.",
        window=(0.0, 20.0), window_why="same reasoning as the kinematic displacement above",
        gwin={"a": (0.5, 10.0)}, gwin_why="as above",
        cap=1.0e4),
    # DROPPED, AND THE REASON IS A RUNTIME LIMIT RATHER THAN A PHYSICS ONE.
    #   ("F=G*m1*m2/(r)^(2)", "r") -- the work done by gravity between two separations.
    # The store declares this record's masses (1e20, 1e30) "planetary or stellar" and its
    # separation (1e6, 1e12) "orbital". At that scale the INTEGRAND itself exceeds the evaluator's
    # range: arity-4 integ is numeric quadrature, and 0 of 15 probed orbital transfers returned a
    # value -- every one !range. It is not the interval width and not the result magnitude; a
    # coefficient of 1 over the same interval evaluates fine.
    #
    # The alternative was a laboratory-scale window, and that is exactly what I tried first: it
    # contradicted the store's own declaration and gate_given_range caught it at 14.10%. A record
    # whose declared scale the runtime cannot evaluate does not get a second declaration invented
    # to make it fit. Four honest accumulations beat five with one fighting both the store and the
    # evaluator.
}

# SPLIT BY WHETHER THE FRAME NAMES THE QUANTITY, because the ANSWER may only name it if the
# QUESTION did. "The displacement is 571 m" against a question that just says "integrate v_0+a*t"
# asserts a record identity the prompt does not contain -- gate_name_provenance caught 88 of them,
# and that gate exists because a model naming a record it cannot read supplies the WRONG name 89.7%
# of the time. Same scope defect as is_refusal/prov_clean: correctness depends on which span the
# value came from, so the two sets are separate rather than one list with a convention.
DEF_FRAMES_NAMED = [
    "{givens} What is {what} as {v} goes from {a} to {b}?",
    "{givens} Find {what} between {v} = {a} and {v} = {b}.",
    "{givens} Work out {what} for {v} running from {a} to {b}.",
]
DEF_FRAMES_BARE = [
    "{givens} Integrate {rhs} with respect to {v} from {a} to {b}.",
    "{givens} What is the definite integral of {rhs} over {v} from {a} to {b}?",
]

DEF_PROSE_NAMED = [
    "{what} is {r} {u}. {why}",
    "{r} {u}. {why}",
    "Evaluating between the limits gives {r} {u}. {why}",
]
DEF_PROSE_BARE = [                       # names no quantity: nothing to source from the prompt
    "{r} {u}. {why}",
    "Evaluating between the limits gives {r} {u}. {why}",
    "{r} {u} -- the antiderivative at {b} minus its value at {a}. {why}",
]

_FN = {"pi", "e", "sin", "cos", "tan", "sqrt", "exp", "ln", "log", "asin", "acos", "atan"}


def _free(expr, var):
    """Identifiers in the expression that must be bound before arity-4 integ will accept it."""
    return [x for x in dict.fromkeys(re.findall(r"[A-Za-z_][A-Za-z0-9_]*", expr))
            if x != var and x not in _FN]


def _sub(expr, vals):
    """Bind symbols to literals, each PARENTHESISED so a leading minus stays an OPERATOR.

    `\\b` treats _ as a word character, so v_0 and v cannot collide. Substituting k=8.31 into
    `-k*x` produced `-8.31*x`, and gate_no_orphan_values reads `-8.31` as one literal, absent from
    a question that says `k = 8.31`: 2.98% of calls using a value the model never saw. `-(8.31)*x`
    is the same expression to the runtime (verified) and leaves the literal exactly as written.
    """
    for k in sorted(vals, key=len, reverse=True):
        expr = re.sub(rf"\b{re.escape(k)}\b", f"({_fmt(vals[k])})", expr)
    return expr


def _num(s):
    try:
        x = float(s)
    except (TypeError, ValueError):
        return None
    return x if x == x and abs(x) != float("inf") else None


def _fmt(x):
    """A bound the tokenizer will not spend ten tokens on, and that reads like a textbook limit."""
    x = float(x)
    if x.is_integer() and abs(x) < 1e6:
        return str(int(x))
    return f"{x:.4g}"


def build_definite(recs, run_tool, rng, draw, dose=None):
    """C3 documents. `draw(rec, var)` returns one sampled value, or None if nothing is DECLARED.

    Returns (documents, skipped, fired). `fired` counts how often each precondition rejected a draw,
    because a bound nobody has watched fire is a claim and not a check.
    """
    dose = max(1, dose if dose is not None else C3_DOSE)
    by_f = {r.get("f"): r for r in recs}
    docs, skipped = [], []
    fired = {"cap": 0, "degenerate": 0, "unbound-given": 0, "integ4": 0, "attempts": 0}
    for (f, v), spec in ACCUM.items():
        r = by_f.get(f)
        if not r:
            skipped.append((f, v, "record-not-in-store", "-"))
            continue
        rhs = f.split("=", 1)[1].strip()
        made, tries = 0, 0
        while made < dose and tries < dose * 40:
            tries += 1
            fired["attempts"] += 1
            vals = {}
            for sym in _free(rhs, v):
                # A SCOPED WINDOW OVERRIDES THE RECORD'S, and is not a second declaration of it:
                # the range table bounds a quantity across every use, this bounds it inside one
                # accumulation, and the narrowing carries its reason in gwin_why.
                gw = (spec.get("gwin") or {}).get(sym)
                x = rng.uniform(*gw) if gw else draw(r, sym)
                if x is None:
                    break
                # SIGNIFICANT FIGURES, NOT DECIMAL PLACES. `round(6.674e-11, 4)` is 0.0, so G
                # became 0, the integrand became `0*926.8*668/(r)^(2)` and every gravitational work
                # document answered 0 J. Third instance in this repo of decimal places eating a
                # small constant, and I wrote it four lines from the comment in _sig saying so.
                vals[sym] = float(_sig(x, 4)) if gw else x
            else:
                w = spec["window"]
                lo_w, hi_w = w(vals) if callable(w) else w
                if not (hi_w > lo_w):
                    fired["degenerate"] += 1
                    continue
                a_v, b_v = rng.uniform(lo_w, hi_w), rng.uniform(lo_w, hi_w)
                lo, hi = min(a_v, b_v), max(a_v, b_v)
                if hi - lo < (hi_w - lo_w) * 0.05:      # a vanishing interval is not a question
                    fired["degenerate"] += 1
                    continue
                a, b = _fmt(lo), _fmt(hi)
                call = f"<tool>integ<arg>{_sub(rhs, vals)}<arg>{v}<arg>{a}<arg>{b}</tool>"
                res = run_tool(call)
                n = _num(res) if res and not res.startswith("!") else None
                if n is None:
                    fired["integ4"] += 1
                    skipped.append((f, v, "integ4", res or "?"))
                    continue
                if abs(n) > spec["cap"]:
                    fired["cap"] += 1
                    continue
                units = r.get("units") or {}
                givens = " ".join(f"{k} = {_fmt(x)}{(' ' + units[k]) if units.get(k) else ''}."
                                  for k, x in vals.items())
                # ALWAYS THE NAMED FRAME. Splitting frames by whether they name the quantity was
                # not enough: the explanatory clause says "integrating a velocity over time gives
                # displacement" in EVERY document, and `displacement` is itself a store record
                # name, so a bare frame left it unsourced. The reading is the teaching value the
                # brief asks for and is not worth dropping, so the question names the quantity and
                # the answer may then use it.
                frames, prose = DEF_FRAMES_NAMED, DEF_PROSE_NAMED
                q = rng.choice(frames).format(givens=givens, what=spec["what"], rhs=rhs,
                                              v=v, a=a, b=b)
                # THE ANSWER ROUNDS; <res> STAYS EXACT. RESULT_RES_SPAN.md: the answer restated the
                # result span byte-for-byte in 100% of documents, so the corpus never demonstrated a
                # rounding step. `res` keeps meaning the runtime's result and nothing borrows it.
                a_val = _sig(n)
                docs.append({"head": f, "kind": "C3", "q": q, "call": call, "res": res,
                             "ans": rng.choice(prose).format(
                                 r=a_val, u=spec["unit"], why=spec["why"], a=a, b=b,
                                 what=spec["what"][0].upper() + spec["what"][1:])})
                made += 1
                continue
            fired["unbound-given"] += 1
            skipped.append((f, v, "no-declared-window", "-"))
            break
    return docs, skipped, fired


def _sig(x, n=4):
    """4 significant figures, which is what the answer states. Not decimal places -- round(3e-9, 4)
    is 0.0, the defect that annihilated every optical wavelength."""
    if x == 0:
        return "0"
    s = f"{x:.{n}g}"
    return s
