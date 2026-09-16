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


# Singular nouns that end in -s. "zeroth law of thermodynamics ARE the law that states" was
# generated because the head test looks only at the last character. -ics is the big family;
# the rest are the ones the glossary actually contains.
_SINGULAR_S = ("ics", "ss", "us", "sis", "ies", "ness", "eous", "ous")


# THE HEAD OF "X of Y" IS X. This repo already recorded the rule and I broke it again:
# "method of adding percents" ends in -s, so the last-word test said plural and generated
# "Method of adding percents ARE the percent uncertainty ...". The project log, verbatim: "A suffix of a
# noun phrase is not a noun phrase, and position is not headedness. English `X of Y` has head `X`."
# 14 of 1,443 terms are affected, and every one of them would have shipped.
_PREP = re.compile(r"\b(of|in|for|from|to|by|with|on|at|between|per|across|through|under|about)\b")


def _head(term):
    t = re.sub(r"\s*\([^)]*\)", "", term).strip()
    m = _PREP.search(t)
    return (t[:m.start()].strip() if m and m.start() > 0 else t).lower()


def _art(term):
    """`is` or `are`, decided by the HEAD noun of the term, not its last word."""
    t = _head(term)
    if not t.endswith("s"):
        return "is"
    return "is" if t.endswith(_SINGULAR_S) else "are"


# THE FRAME IS CHOSEN TO SUIT THE MEANING; NO ARTICLE IS EVER INSERTED.
#
# OpenStax writes a glossary meaning as a BARE NOUN PHRASE, because it is read after "term:" in a
# two-column table. Splicing that after "X is" is broken English on 884 of 1,443 entries:
#
#   "Dielectric constant is factor by which capacitance increases when a dielectric is inserted"
#
# The first fix inserted "the" when the meaning looked like a bare noun. That is a heuristic about
# English, and it misfired the way heuristics in this repo always do -- three ways at once, on four
# entries, found by READING them:
#
#   "is THE DEFINED AS B=(mu_0I)/(2pir)"      the meaning starts with a past participle
#   "are the law that states ..."             "thermodynamics" read as a plural
#   and the four extra characters pushed three of them past the cap, so the trimmer ate the
#   definition those documents exist to carry.
#
# So the heuristic is gone. A meaning that already begins with a determiner or a verb takes the
# "X is ..." frames; a bare noun phrase takes the COLON frame, which is grammatical after anything.
# Nothing is guessed and nothing is inserted.
_DETERMINED = re.compile(
    r"^(a|an|the|any|all|each|every|one|two|three|no|not|when|where|how|what|which|that|this|"
    r"those|these|its|it|in|on|at|for|to|of|by|from|with|"
    r"is|are|was|were|has|have|can|will|must|refers|describes|measures|occurs|equals|"
    r"defined|derived|given|measured|expressed|found|calculated|stated|written|represented|"
    r"used|equal|same|half|twice|either|both|such)\b", re.I)


# A meaning that already contains its own main verb is a SENTENCE, not a complement.
# "the physical law that states that the magnetic field ... IS proportional to the current" after
# "Ampere's law is" gives two predicates in one sentence. 145 of 1,443 meanings are like this, and
# they read as broken because they are. They take the COLON frame, where a sentence is fine.
_CLAUSE = re.compile(r"^(?:the|a|an)\b[^.;]{0,90}?\b(is|are|was|were)\b", re.I)


def _determined(meaning):
    """True when the meaning can follow "X is" as written: determined AND not already a clause."""
    m = meaning.strip()
    if not m or _CLAUSE.match(m):
        return False
    return bool(_DETERMINED.match(m)) or m[0].isupper() or m[0].isdigit()


# WHY THERE ARE THIS MANY. The first banks had four frames each, which for a given term collapsed
# to about two distinct strings: 26,424 K1 documents carried 2,743 distinct answers, so the model
# saw one string 8 to 16 times per term. That teaches recitation of a frame, which is the failure
# docs/PHRASING.md exists about, and it is measurable before training rather than after.
#
# Content variety still comes from the 1,443 meanings; these vary the WRAPPER so the wrapper is not
# what gets learned. Each bank is checked for duplicates by test_knowledge_docs.

# Frames that require the meaning to follow "X is" grammatically.
K1_FRAMES_IS = [
    "{T} {v} {m}.",
    "{T} {v} {m}. {E}",
    "Short version: {T} {v} {m}.",
    "In plain terms, {t} {v} {m}.",
    "{T} {v} {m}. That is the whole of it.",
    "Here is the idea: {t} {v} {m}.",
    "{T} {v} {m}, and that is what the term covers.",
    "The short answer: {t} {v} {m}.",
    "{T} {v} {m}. Worth knowing: {E}",
    "When someone says {t}, they mean {m}.",
    "{T} {v} {m}, and that is the definition physicists use.",
    "Put simply, {t} {v} {m}.",
]
# Frames that work after ANY noun phrase, used when the meaning is a bare one.
K1_FRAMES_NP = [
    "{T}: {m}.",
    "{T}: {m}. {E}",
    "{T} means {m}.",
    "{T} refers to {m}.",
    "The term {t} means {m}.",
    "{T}: {m}. That is the whole of it.",
    "Here is the idea. {T}: {m}.",
    "{T} describes {m}.",
    "When someone says {t}, they mean {m}.",
    "{T}: {m}. Worth knowing: {E}",
    "Short version. {T}: {m}.",
    "{T} is the name for {m}.",
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
    "I cannot answer that: what I have is what {t} means, not a way to calculate it.",
    "I cannot answer that: the record explains {t}. It does not give a relation to put numbers "
    "into.",
    "I cannot answer that: there is no formula here, only the definition of {t}.",
    "I cannot answer that: this record says what {t} is, so there is no quantity to work out "
    "from it.",
    "I cannot answer that: computing {t} needs a relation, and the record I have is a definition.",
    "I cannot answer that: the record defines {t}. Nothing in it can be evaluated.",
]

def ask_agrees(question, term):
    """Fix subject-verb agreement in a question frame for a PLURAL term.

    "what is beta rays?" was generated for 153 of 1,443 terms. The frames in asks_explain.py are
    written for a singular noun because most terms are one, and substituting a plural into them
    produces a question no student would type -- which teaches the model that this is how students
    write. Cheap to fix, invisible to every structural check.
    """
    if _art(term) != "are":
        return question
    # ONLY WHEN THE TERM IS THE SUBJECT. The first version matched the prefix "what is " and
    # rewrote "what is the definition of adhesive forces" to "what ARE the definition of adhesive
    # forces" -- 455 documents, caught by gate_ask_quantity, which flags exactly this shape because
    # 63% of a hand-read sample was ill-posed the last time a frame like it was in use. The subject
    # there is "the definition", singular; the plural term is the object of a preposition.
    for a, b in (("what is ", "what are "), ("What is ", "What are "),
                 ("what's ", "what are "), ("whats ", "what are "),
                 ("what exactly is ", "what exactly are ")):
        if question.startswith(a) and question[len(a):].lower().startswith(term.lower()):
            return b + question[len(a):]
    return question


# Compute-intent question frames for K3. Hand-written for the same reason asks_explain.py is: A6.
K3_ASK = [
    "find {t}.", "calculate {t}.", "work out {t}.", "compute {t}.",
    "what is the value of {t}?", "how much is {t}?", "solve for {t}.",
    "find the {t} here.", "give me a number for {t}.", "determine {t}.",
]


# SOFT target and HARD ceiling. The device caps an answer at 90 tokens (device_app.c:524), which is
# about 230 characters at the measured 2.564 chars/token, so the hard ceiling sits just under it.
SOFT_CAP = 215
HARD_CAP = 228


def k1_answer(term, meaning, elaboration, rng, cap=SOFT_CAP):
    """One K1 answer. THE MEANING IS NEVER DROPPED.

    The first version returned out[:cap], which silently truncated 80 of 1,443 answers mid-phrase
    and removed the definition from the document whose whole job is to carry it -- and the gate's
    HIT leg looks for rare words OF THE MEANING, so those 80 could never have passed. Now the
    ELABORATION is what goes when space runs out, and a meaning longer than the soft cap gets the
    bare frame and the hard ceiling. Nothing is cut mid-word: `_fit` trims at a word boundary and
    only as a last resort, and the count of answers that reach it is reported by the caller.
    """
    v = _art(term)
    T = term[0].upper() + term[1:] if term[:1].islower() else term
    m = _clean(meaning)
    det = _determined(m)
    bank = K1_FRAMES_IS if det else K1_FRAMES_NP
    bare = "{T} {v} {m}." if det else "{T}: {m}."
    f = rng.choice(bank)
    e = (elaboration or "").strip()
    if "{E}" in f and not e:
        f = bare
    out = re.sub(r"\s+", " ", f.format(T=T, t=term, v=v, m=m, E=e)).strip()
    if len(out) > cap:
        out = re.sub(r"\s+", " ", bare.format(T=T, t=term, v=v, m=m)).strip()
    if len(out) > HARD_CAP:
        out = _fit(out, HARD_CAP)
    return out


def _fit(s, cap):
    """Trim at a CLAUSE boundary where one exists, else a word boundary. Never mid-word.

    Trimming at a word boundary alone left "... from m_i down." and "... converted into mechanical
    work in the." -- a dangling fragment, which is the defect this repo already recorded as
    "Where ." and fixed once in the GIVE frames. A comma or semicolon is a place a sentence can
    legitimately stop; the middle of a prepositional phrase is not.
    """
    if len(s) <= cap:
        return s
    cut = s[:cap - 1]
    for sep in (";", ", ", " "):
        i = cut.rfind(sep)
        if i > cap // 2:
            cut = cut[:i]
            break
    return cut.rstrip(" ,;:-") + "."


def k2_answer(term, other, rng):
    return rng.choice(K2_FRAMES).format(t=term, O=other, v=_art(term))


def k3_answer(term, rng):
    return rng.choice(K3_FRAMES).format(t=term)
