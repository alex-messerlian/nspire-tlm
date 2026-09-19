#!/usr/bin/env python3
"""F1: two-turn documents, so a follow-up question is a shape the model has seen.

WHY THIS EXISTS. `app_context()` in src/store/app.c builds a conversation history -- three tiers, a
character budget, the two most recent turns verbatim -- and NOTHING CALLS IT. Every turn on the
calculator is turn one. docs/RESULT_FOLLOWUP_UNWIRED.md records it as an OPEN defect rather than an
exemption, and says exactly why wiring the device alone would be wrong:

    A second turn's prompt contains the first turn's question. That shape occurs in 0 of 239,853
    training documents. A shape with zero documents is not a weak class, it is an untrained one.

RESULT_CANNOT_EXPLAIN is this repo's record of what happens when the runtime emits a prompt shape
the corpus does not have: the model produced a self-contradiction and it read as a model failure.
So the two halves ship together. This is the corpus half.

THE PROMPT IS BUILT BY THE DEVICE, NOT BY THIS FILE. `app_context` emits "Earlier: <q1> " and
app_request concatenates it with the new question, so the whole prompt is what
`build/devprompt` returns for "Earlier: {q1} {q2}" -- question surface, given list, record span,
missing field and all. Building it here would be a second implementation of the assembler, which is
the defect this repo has found five times (units_train, store.tns, units_holdout, _MICRO_CVAL,
build_splits). The generator passes a callable and this file never formats a record.

TWO CLASSES, AND BOTH ARE UNANSWERABLE WITHOUT THE EARLIER TURN -- which is the point. A follow-up
that stands on its own is not a follow-up and would teach nothing about reading context.

  CHANGE  turn 1 computes; turn 2 restates ONE value ("now v = 5"). The new value alone names no
          quantity and no relation: "now v = 5" retrieves `wedge` on its own (measured). It is
          answerable only because the earlier turn supplies the relation and the other givens.
          A113 makes the restated value override rather than appear twice.
  WHY     turn 2 asks about the relation itself. Every answer is derivable FROM THE RECORD SPAN --
          the units are its second field and the variables are in its first -- so nothing is
          recalled and provenance holds. "why is it negative" is emitted only for records that
          actually carry a minus sign.
"""
import os, re, sys, pathlib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from calculus import _sub, _fmt            # ONE substitution, parenthesised; never a second copy
from recfmt import fields, FORMULA, UNITS  # " | " is the separator; a formula may hold a bare pipe

# DOSE 36, and the number is reasoned rather than picked. A112 measured that a question frame seen
# ~3.4 times per record is learned at 70% and one seen ~10 times at 100%. F1 has ~17 frames, so
# dose 14 gave roughly ONE exposure per (record, frame) -- thinner than the arm A112 had to fix.
#
# It does not need the full ~10, because F1's answers are DERIVABLE rather than memorised: the
# units are the record span's second field, the dependency list is its first, and the compute class
# is ordinary arithmetic with a prefix. What has to be learned is one behaviour -- an "Earlier:"
# prefix means read the context and answer the NEW question -- and every document teaches it.
# 36 gives ~2.5 exposures per frame and ~4,100 documents, about 1.3% of the corpus, which is the
# same order as C3 and does not move the mix.
DOSE = int(os.environ.get("F1_DOSE", "36"))
_FN = {"pi", "e", "sin", "cos", "tan", "sqrt", "exp", "ln", "log", "asin", "acos", "atan"}

# THE VALUES COME AFTER A FULL STOP, and that is measured rather than stylistic. ns_assemble
# STRIPS every assignment out of the question and appends the given list canonically, so a frame
# with values mid-sentence strips to rubble: "calculate {n} with {g}" became
# "calculate hooke's law for what." and "find {n} when {g}" became "find ... when what units is
# that in?". With the values in their own clause the prose survives intact.
ASK1 = ["calculate {n}. {g}", "find {n}. {g}", "work out {n}. {g}",
        "what is {n}. {g}", "compute {n}. {g}"]

# The restatement. The VARIABLE NAME IS STRIPPED with its value, so these are chosen to leave a
# sentence that still reads: "now v = 5" -> "now", "recompute with v = 5" -> "recompute".
ASK_CHANGE = ["recompute. {v} = {x}", "again. {v} = {x}", "same question. {v} = {x}",
              "redo it. {v} = {x}", "once more. {v} = {x}", "try again. {v} = {x}"]

# AND THE ANSWER MAY NOT NAME WHAT CHANGED. The first version wrote "Only k changed; the relation
# is the same" -- and the prompt the device builds carries ONLY the final given list, so which
# variable was restated is not in it. That is the provenance defect this repo keeps paying for: an
# answer asserting something it cannot read. The result is stated and nothing else is claimed.
ANS_CHANGE = ["{r}.", "That gives {r}.", "{r}, from {f}.",
              "Now it comes to {r}.", "{f} gives {r}."]

ASK_UNITS = ["what units is that in?", "what unit is {l} in?", "and the units?",
             "what are the units on that?"]
ANS_UNITS = ["{l} is in {u}.", "That is in {u}.", "{l} comes out in {u}."]

ASK_DEP = ["what does it depend on?", "what goes into that?", "which quantities matter there?",
           "what do i need to know for that?"]
ANS_DEP = ["{f} depends on {d}.", "It depends on {d}, through {f}.",
           "You need {d}. That is what {f} uses."]

ASK_NEG = ["why is it negative?", "why the minus sign?", "what does the minus mean?"]
ANS_NEG = ["The minus in {f} means it points opposite to what is growing, not that it is small.",
           "{f} carries a minus because the two quantities move in opposite directions.",
           "The sign in {f} is direction, not size."]


def _free(rhs, lhs):
    return [x for x in dict.fromkeys(re.findall(r"[A-Za-z_][A-Za-z0-9_]*", rhs))
            if x not in _FN and x != lhs]


def build(recs, prompt_of, run_tool, rng, draw, holds, unit_of, dose=None):
    """F1 documents.

    prompt_of(text)     -> the device's full "<q>...</q><r>..." prefix, or None.
    run_tool(call)      -> the runtime's <res> string.
    draw(rec, var)      -> a sampled value, or None when nothing is DECLARED for it.
    holds(rec, vals)    -> generate.preconditions_hold. A NEW TIER THAT DRAWS GIVENS MUST APPLY THE
                           SHARED CROSS-VARIABLE PRECONDITIONS. The first version did not, and the
                           hand-read immediately produced a heat engine with Q_c = 519 against
                           Q_h = 24.2 -- negative work, a refrigerator -- and an output work of
                           -127.3 J after friction. BOTH records already carry a correct
                           precondition in _PRECONDITION; this tier simply never asked. Same class
                           as every other "the check exists and the new consumer does not call it".
    unit_of(rec)        -> generate.lhs_unit. NOT `units[lhs]`: a dimensionless record must render
                           BARE ("0.25", not "0.25 1") and lhs_unit is where that ruling lives.
    """
    dose = dose if dose is not None else DOSE
    docs, skipped = [], []
    fired = {"no-prompt": 0, "wrong-record": 0, "tool": 0, "no-window": 0,
             "precondition": 0, "attempts": 0}
    for r in recs:
        f = r.get("f") or ""
        name = (r.get("name") or "").split(",")[0].strip().lower()
        if "=" not in f or not name:
            continue
        lhs, rhs = f.split("=", 1)[0].strip(), f.split("=", 1)[1].strip()
        free = _free(rhs, lhs)
        units = r.get("units") or {}
        if not free or lhs not in units:
            continue
        made = 0
        for _ in range(dose * 4):
            if made >= dose:
                break
            fired["attempts"] += 1
            vals = {}
            for v in free:
                x = draw(r, v)
                if x is None:
                    break
                vals[v] = x
            else:
                if not holds(r, vals):
                    fired["precondition"] += 1
                    continue
                u = unit_of(r)
                g = ", ".join(f"{v} = {_fmt(x)}" for v, x in vals.items())
                q1 = rng.choice(ASK1).format(n=name, g=g)
                # A UNARY MINUS, NOT A SUBTRACTION. `"-" in rhs` fired on W=Q_h-Q_c and answered
                # "it points opposite to what is growing" -- which is true of F=-k*x and false of a
                # difference of two heats. Wrong supervision that every structural check passes:
                # the document is well-formed, the record is right, the prose is fluent, and the
                # physics is not about that relation. Only a leading minus is a sign convention.
                kind = rng.choice(["change", "change", "dep"] + (["units"] if u else []) +
                                  (["neg"] if rhs.lstrip().startswith("-") else []))
                if kind == "change":
                    vk = rng.choice(free)
                    nx = draw(r, vk)
                    if nx is None or _fmt(nx) == _fmt(vals[vk]):
                        fired["no-window"] += 1
                        continue
                    q2 = rng.choice(ASK_CHANGE).format(v=vk, x=_fmt(nx))
                    nvals = dict(vals); nvals[vk] = nx
                    if not holds(r, nvals):        # the RESTATED draw must hold it too
                        fired["precondition"] += 1
                        continue
                    call = f"<tool>eval<arg>{_sub(rhs, nvals)}</tool>"
                    res = run_tool(call)
                    if not res or res.startswith("!"):
                        fired["tool"] += 1
                        continue
                    ans = rng.choice(ANS_CHANGE).format(r=f"{_sig(res)} {u}".strip(), f=f)
                else:
                    q2 = {"units": rng.choice(ASK_UNITS).format(l=lhs),
                          "dep": rng.choice(ASK_DEP),
                          "neg": rng.choice(ASK_NEG)}[kind]
                    call, res = "", ""
                    ans = {"units": rng.choice(ANS_UNITS).format(l=lhs, u=u),
                           "dep": rng.choice(ANS_DEP).format(f=f, d=_and(free)),
                           "neg": rng.choice(ANS_NEG).format(f=f)}[kind]

                pre = prompt_of(f"Earlier: {q1} {q2}")
                if not pre:
                    fired["no-prompt"] += 1
                    continue
                # THE RECORD THE DEVICE RETRIEVED MUST BE THE ONE THE ANSWER IS ABOUT. The follow-up
                # is concatenated with the earlier turn precisely so retrieval can see the relation;
                # when it still lands elsewhere the document would teach an answer about a record
                # the prompt does not show, which is the A6 wrong-supervision class.
                if fields(pre.split("<r>", 1)[1])[FORMULA] != f:
                    fired["wrong-record"] += 1
                    skipped.append((f, kind, "wrong-record"))
                    continue
                docs.append({"head": f, "kind": "F1", "pre": pre, "call": call, "res": res,
                             "ans": ans})
                made += 1
    return docs, skipped, fired


def _and(xs):
    xs = list(xs)
    return xs[0] if len(xs) == 1 else ", ".join(xs[:-1]) + " and " + xs[-1]


def _sig(s, n=4):
    """The ANSWER rounds; <res> stays exact (RESULT_RES_SPAN.md)."""
    try:
        return f"{float(s):.{n}g}"
    except (TypeError, ValueError):
        return s
