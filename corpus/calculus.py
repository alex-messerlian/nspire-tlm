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
