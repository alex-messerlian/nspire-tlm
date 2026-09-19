#!/usr/bin/env python3
"""R1: rearrangement. Isolate a named variable in a record's own relation, through `solve`.

WHY THIS CLASS EXISTS. Measured on the shipped store: all 164 records carry >=2 variables, so the
store admits 375 distinct "solve for X" problems, and the corpus called `solve` in ZERO documents
against `eval` in 192,754. The tool has worked the whole time. This is the math that connects to
the physics already on the calculator -- every formula becomes an object you can ask a different
question of -- and it was the largest capability in the tree with no curriculum behind it.

THE ANSWER IS NOT THE <res> SPAN RESTATED. A44 records what that costs: when the answer copied
<res> byte-for-byte in 100% of documents, the corpus never demonstrated a step, and the model
learned that answering IS copying. Here the tool returns `a=F/m` and the answer has to say what
that means in words as well as show it, which is the same discipline the F1 explanations imposed on
the compute tier.

THE 37 UNSOLVABLE PAIRS ARE SKIPPED, NOT FAKED. `solve` returns !nosol on square-root inversions
(f=(1/2pi)sqrt(k/m) for k, the distance formula for x_1). That is the tool correctly refusing rather
than guessing, and a document whose <res> is an error code teaches the model to ask for something it
cannot have. They are counted and reported, never silently dropped: "cannot generate" and "generated
nothing" must not share an exit.
"""
import re

# Dose per (record, variable) pair. 338 solvable pairs.
#
# 30, NOT 10, AND THE UNIT THAT MATTERS IS TOOL CALLS RATHER THAN DOCUMENTS. At 10 this class is
# 3,380 documents, which is 1.7% of the corpus's tool calls against `eval`'s 192,754. The model is
# not being taught a new topic here, it is being taught that a SECOND TOOL EXISTS and when to reach
# for it, and a pattern seen on 1.7% of the occasions a tool is called is thin for that. At 30 it is
# 10,140 documents and about 5%, which is the same order as the knowledge tier's share of its own
# surface.
#
# This is a judgement, not a measurement, and it is the kind the probe settles: D7 (below) reports
# how often a rearrangement question actually produces a solve call, and if that is high the dose
# can come back down at no cost to anything else.
import os
DOSE = int(os.environ.get("R1_DOSE", "30"))

# HOW A LAZY STUDENT ASKS FOR A REARRANGEMENT. Written, not mined: A6 measured that mining question
# surfaces yielded 8 usable frames and 36 sentence fragments, and substituting into all 44 blind put
# a 22.5% wrong-quantity defect into the corpus.
FRAMES = [
    "solve {f} for {v}",
    "rearrange {f} for {v}",
    "solve for {v} in {f}",
    "make {v} the subject of {f}",
    "isolate {v} in {f}",
    "{f}, solve for {v}",
    "rearrange {f} to get {v}",
    "what is {v} in {f}",
    "get {v} out of {f}",
    "{f} -- solve for {v}",
    "how do i solve {f} for {v}",
    "write {f} in terms of {v}",
]

# The prose frames. {v} the isolated symbol, {r} the FULL REARRANGED EQUATION ("k=-F/x", not
# "-F/x"), {name} the record's name. That distinction cost a frame: "Put {v} on the left and {r}"
# read as "Put k on the left and k=-F/x", which is redundant and ungrammatical. Found by reading
# four generated documents, which is the step that precedes generating at volume.
# {rest} the other symbols in words. Twelve, because three identical phrasings across 3,400
# documents is a template the model learns instead of the content.
PROSE = [
    "Isolating {v} gives {r}.",
    "{r}. That is {name} with {v} made the subject.",
    "Rearranged for {v}: {r}.",
    "{r} -- same relation, {v} on its own.",
    "Solving for {v} gives {r}.",
    "{r}. Nothing new is assumed; the relation is just written the other way round.",
    "{v} comes out as {r}.",
    "Put {v} on the left: {r}.",
    "{r}, which is what you use when {v} is the unknown.",
    "{r}. Same physics, different unknown.",
    "That rearranges to {r}.",
    "{r} -- read it off whenever {v} is what you are missing.",
]


def _ok_ident(s):
    return bool(re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", s or ""))


def pairs(recs):
    """Every (record, variable) a rearrangement could be asked about. LHS excluded: asking to
    isolate the variable the relation already solves for is not a rearrangement."""
    out = []
    for r in recs:
        f = r.get("f") or ""
        if "=" not in f:
            continue
        lhs = f.split("=", 1)[0].strip()
        for v in (r.get("units") or {}):
            if v != lhs and _ok_ident(v):
                out.append((r, v))
    return out


def build(recs, run_tool, rng, dose=DOSE):
    """Return (documents, skipped). `run_tool` takes a full <tool>...</tool> string and returns the
    text between <res> and </res>, which is the ONE evaluator the corpus and the device share."""
    docs, skipped = [], []
    for r, v in pairs(recs):
        f = r["f"]
        call = f"<tool>solve<arg>{f}<arg>{v}</tool>"
        res = run_tool(call)
        if not res or res.startswith("!"):
            skipped.append((f, v, res or "?"))
            continue
        name = r.get("name") or f
        for _ in range(dose):
            q = rng.choice(FRAMES).format(f=f, v=v)
            ans = rng.choice(PROSE).format(v=v, r=res, name=name)
            docs.append({"head": f, "kind": "R1", "q": q, "call": call, "res": res, "ans": ans})
    return docs, skipped
