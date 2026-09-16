#!/usr/bin/env python3
"""The knowledge tier's three document classes, built against the K-TEXT record shape.

THE PROMPT THE DEVICE BUILDS for a definition, byte-verified through build/devprompt:

    <q>QUESTION</q><r>TERM | term:text | missing:none | MEANING | fit:high

Nothing here invents that shape. `span()` reproduces what src/store/assemble.c emits for a K-TEXT
record, and tools/eval/gate_knowledge_bytes.py diffs the two, because five separate defects in this
repo were one field of a record span disagreeing between the generator and the device.

THE THREE CLASSES, AND WHY THEY ARE A SET AND NOT A FEATURE.

    K1  explain-hit      the record defines the asked term        -> explain it
    K2  explain-miss     the record defines SOMETHING ELSE        -> say what it defines, decline
    K3  compute-on-def   the record defines the asked term, and
                         the question asks to COMPUTE             -> refuse, nothing to compute with

Adding K1 alone would teach "a record with `term:text` means explain", which is a surface rule a
model can apply without reading anything. That is RESULT_A42_SHAPE_CUE exactly: `fit_m` scored
98.9% on a rule that was a 100%-precision classifier over 12,014 firings, and both pre-registered
guards passed because neither varied the input the defect lived in. Here the input is what the
record says versus what the question asks, and K2 and K3 are what vary it. Their rates are not
tuning knobs: K3 exists to hold the explain/refuse base rate inside a `term:text` prompt close to
the rate inside a formula prompt, so record type does not decide the answer by itself.

THE SPARE GIVEN IS DRAWN BEFORE THE CLASS, deliberately. In the shipped corpus an ANSWER document
carries a spare given in 0 of 197,562 and a D2 in 99.7%, which made "a variable appears that this
record does not use" a perfect refusal cue. Here P(a number is present) is identical across K1, K2
and K3 by construction, so the feature carries zero bits and cannot become one.
"""
import hashlib
import random
import re

KVAR = "term"
KUNIT = "text"

# Dose strata. The only parameter that can be varied WITHIN one training run, which is what makes
# the 30-point training-seed term common-mode instead of needing nine runs to resolve.
DOSE = (8, 16, 32)

# How often a knowledge question carries a number it does not need. Matched across all three
# classes; see the module docstring.
SPARE_GIVEN = 0.30


def span(term, meaning):
    """The record span, exactly as src/store/assemble.c emits it for a K-TEXT record."""
    return f"{term} | {KVAR}:{KUNIT} | missing:none | {meaning} | fit:high"


def stratum(term):
    """Which dose stratum a term belongs to. Hashed, so it is stable across runs and independent
    of file order: a term cannot change dose because something else was added to the glossary."""
    h = hashlib.sha256(("kdose/v1:" + term.lower()).encode()).digest()
    return int.from_bytes(h[:4], "big") % len(DOSE)


# ---------------------------------------------------------------------------------------------
# ANSWERS
#
# Every answer is composed from WHAT IS IN THE PROMPT plus, for K1, at most one elaboration
# sentence that the model must memorise. That split is deliberate and it is the thing the tier is
# testing: the meaning is readable from field 3, the elaboration is not, so the two are separable
# by an eval and are reported separately.
# ---------------------------------------------------------------------------------------------

def _clean(s):
    s = re.sub(r"\s+", " ", s).strip()
    return s[:-1] if s.endswith(".") else s


def _art(term):
    """`is` or `are`, because "kinetic energies is" reads as broken English to a reader and the
    corpus is what teaches the register."""
    t = term.strip().lower()
    return "are" if (t.endswith("s") and not t.endswith("ss") and not t.endswith("us")) else "is"


K1_FRAMES = [
    "{T} {v} {m}.",
    "{T} {v} {m}. {E}",
    "{m}, and that {v} what {t} means.",
    "{T} {v} {m}. {E}",
    "Short version: {T} {v} {m}.",
    "{T} {v} {m}.",
]

# K2. The record defines something else, so the answer must NAME what it does define and decline.
# Naming it is what makes the refusal readable rather than reflexive: a model that has not read the
# record cannot produce the other term.
K2_FRAMES = [
    "I cannot answer that: the record I have defines {O}, not {t}.",
    "I cannot answer that: this record defines {O}. It says nothing about {t}.",
    "I cannot answer that: what I have here is the definition of {O}, not of {t}.",
    "I cannot answer that: the record defines {O}, so it cannot tell you what {t} {v}.",
]

# K3. The question asks to compute and the record is a definition. The refusal must say WHY.
K3_FRAMES = [
    "I cannot answer that: the record defines {t} rather than giving a relation, so there is "
    "nothing here to compute with.",
    "I cannot answer that: this is the definition of {t}, not a formula, so no quantity follows "
    "from it.",
    "I cannot answer that: {t} is defined here, not computed. The record carries no relation to "
    "evaluate.",
    "I cannot answer that: the record gives the meaning of {t}, not an equation, so I cannot "
    "work out a number.",
]

# Compute-intent question frames for K3. Hand-written for the same reason asks_explain.py is: A6.
K3_ASK = [
    "find {t}.", "calculate {t}.", "work out {t}.", "compute {t}.",
    "what is the value of {t}?", "how much is {t}?", "solve for {t}.",
    "find the {t} here.", "give me a number for {t}.", "determine {t}.",
]


def k1_answer(term, meaning, elaboration, rng, cap=215):
    v = _art(term)
    T = term[0].upper() + term[1:] if term[:1].islower() else term
    f = rng.choice(K1_FRAMES)
    e = (elaboration or "").strip()
    if "{E}" in f and not e:
        f = "{T} {v} {m}."
    out = f.format(T=T, t=term, v=v, m=_clean(meaning), E=e)
    out = re.sub(r"\s+", " ", out).strip()
    if len(out) > cap:                       # the elaboration is what goes, never the definition
        out = f"{T} {v} {_clean(meaning)}."
    return out[:cap]


def k2_answer(term, other, rng):
    return rng.choice(K2_FRAMES).format(t=term, O=other, v=_art(term))


def k3_answer(term, rng):
    return rng.choice(K3_FRAMES).format(t=term)
