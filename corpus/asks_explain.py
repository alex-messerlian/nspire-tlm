#!/usr/bin/env python3
"""Question surfaces for the knowledge tier, and the lazy-typing mangler that roughens them.

WHY HAND-WRITTEN. A6 is the standing lesson: 44 "mined OpenStax question openings" turned out to be
8 usable frames and 36 sentence fragments, and substituting into all 44 blind put a wrong-quantity
question in 22.5% of the shipped corpus. Mining question surfaces yielded eight generic verbs that
could have been hand-written. So these are hand-written, and every one takes a bare noun phrase as
its object and reads as a sentence with one substituted.

WHY SO MANY, AND WHY THE MANGLER. The device now has to retrieve with no suggestion UI, from a
student who is typing fast on a calculator keypad. The brief, in the user's words: "students are
lazy, students are gonna make mistakes, students are gonna mistype a lot ... think of a hundred
ways hooke's law could be spelled differently."

Two separate defences, and they are NOT the same defence:

  * RETRIEVAL robustness is A56, on the device: bounded Levenshtein in askparse.c, measured at 79.3%
    name-bearing retrieval@1 with EVERY word misspelled against 14.4% without.
  * MODEL robustness is this file. The picker can find the right record and the model still has to
    recognise a question it has never seen phrased that way. A56 does nothing for that, because the
    question reaches the model as the student typed it.

THE MANGLER IS THE PART THAT CAN GO WRONG, so its rate is measured and it is applied at a declared
fraction rather than everywhere. A corpus where every question is misspelled teaches that correct
spelling is out of distribution. `variety()` reports what the bank actually produces.
"""
import random
import re
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

# ---------------------------------------------------------------------------------------------
# FRAMES. {t} is the term, a bare noun phrase: "Hooke's law", "kinetic energy", "the Doppler
# effect". Grouped by register because the mix is a parameter, not an accident: a corpus that is
# 90% "define {t}." teaches a register, not a task.
# ---------------------------------------------------------------------------------------------

# 1. The direct question. What a student types when they know what they want.
ASK_DIRECT = [
    "what is {t}?",
    "what's {t}?",
    "what is {t}",
    "define {t}.",
    "definition of {t}",
    "what is the definition of {t}?",
    "{t}?",
    "{t} definition",
    "meaning of {t}",
    "what does {t} mean?",
    "what do we mean by {t}?",
    "what is meant by {t}?",
    "whats the definition of {t}",
    "{t} means what?",
    "what exactly is {t}?",
    "what is {t} exactly?",
    "define the term {t}.",
]

# 2. The request to explain. The register the user asked for by name.
ASK_EXPLAIN = [
    "explain {t}.",
    "explain {t} to me.",
    "can you explain {t}?",
    "please explain {t}.",
    "explain {t} simply.",
    "explain {t} in simple terms.",
    "explain {t} like I'm new to physics.",
    "give me a quick explanation of {t}.",
    "walk me through {t}.",
    "break down {t} for me.",
    "help me understand {t}.",
    "I need help with {t}.",
    "tell me about {t}.",
    "teach me {t}.",
    "give me the basics of {t}.",
    "a short explanation of {t} please.",
    "run me through {t}.",
    "what should I know about {t}?",
    "explain {t} for a beginner.",
    "can you go over {t}?",
    "explain {t} step by step.",
]

# 3. The confession. A student who is stuck says so, and it is still a request for the same thing.
ASK_STUCK = [
    "I don't get {t}.",
    "I don't understand {t}.",
    "I'm confused about {t}.",
    "{t} makes no sense to me.",
    "still lost on {t}.",
    "can someone explain {t}?",
    "what even is {t}?",
    "no idea what {t} is",
    "never understood {t}",
    "struggling with {t}",
    "I am stuck on {t}.",
    "cant figure out {t}",
    "{t} is confusing.",
    "having trouble with {t}",
    "what am I missing about {t}?",
]

# 4. The mechanism question. Asks for the same content with a different opening word, and these are
# the ones a bank of "what is" frames would never cover.
ASK_MECHANISM = [
    "how does {t} work?",
    "how does {t} actually work?",
    "why does {t} happen?",
    "what causes {t}?",
    "when do you use {t}?",
    "when does {t} apply?",
    "where is {t} used?",
    "what is {t} used for?",
    "why do we care about {t}?",
    "why is {t} important?",
    "what is the point of {t}?",
    "what is {t} good for?",
    "what does {t} depend on?",
    "how is {t} measured?",
    "what is {t} in physics?",
    "how do you find {t}?",
    "what happens in {t}?",
    "what is the idea behind {t}?",
]

# 5. The exam register. How it is phrased on a homework sheet, which is what a student copies in.
ASK_EXAM = [
    "state {t}.",
    "describe {t}.",
    "briefly describe {t}.",
    "in your own words, describe {t}.",
    "summarise {t}.",
    "give a short account of {t}.",
    "discuss {t}.",
    "outline {t}.",
    "explain the concept of {t}.",
    "explain the term {t}.",
    "write a short note on {t}.",
    "explain {t} with an example.",
    "give an example of {t}.",
    "define {t} and say why it matters.",
    "state and explain {t}.",
    "what is {t}? explain briefly.",
]

# 6. The follow-up opening. A second turn does not repeat the first, and the corpus has 0 documents
# of this shape anywhere. Kept separate so the mix can be measured and reported.
ASK_FOLLOWUP = [
    "and {t}?",
    "what about {t}?",
    "ok but what is {t}?",
    "then what is {t}?",
    "so what is {t} exactly?",
    "how does that relate to {t}?",
    "is that the same as {t}?",
    "wait, what is {t}?",
    "can you go deeper on {t}?",
    "more on {t}",
    "one more thing, {t}?",
    "related question, what is {t}?",
]

# 7. Terse. A calculator keypad is slow, so a real question is often three words.
ASK_TERSE = [
    "{t}",
    "{t} explain",
    "{t} meaning",
    "define {t}",
    "explain {t}",
    "what is {t} ",
    "{t} help",
    "{t} pls",
    "whats {t}",
    "{t} basics",
    "{t} simple",
    "{t} definition pls",
]

BANKS = {
    "direct": ASK_DIRECT, "explain": ASK_EXPLAIN, "stuck": ASK_STUCK,
    "mechanism": ASK_MECHANISM, "exam": ASK_EXAM, "followup": ASK_FOLLOWUP,
    "terse": ASK_TERSE,
}

# Weights. `direct` and `explain` dominate because they are what a student actually types; the rest
# are present so the model does not key on an opening word. Declared here rather than implied by
# list length, because a bank's length is a fact about how much I wrote, not about how often the
# register occurs.
WEIGHTS = {"direct": 0.26, "explain": 0.26, "stuck": 0.08, "mechanism": 0.16,
           "exam": 0.12, "followup": 0.04, "terse": 0.08}
assert abs(sum(WEIGHTS.values()) - 1.0) < 1e-9, "register weights must sum to 1"
assert set(WEIGHTS) == set(BANKS), "every bank needs a declared weight"

ALL_FRAMES = [f for b in BANKS.values() for f in b]
assert all("{t}" in f for f in ALL_FRAMES), "every frame must take the term"
assert len(set(ALL_FRAMES)) >= 100, f"only {len(set(ALL_FRAMES))} distinct frames; the brief was a hundred"


# ---------------------------------------------------------------------------------------------
# THE LAZY-TYPING MANGLER
#
# Each rule is something a real student does on this keypad, not a random character edit. The
# apostrophe one is not a guess: the first device test failed on exactly this -- "what is hookes
# law" returned Snell's law, because the user could not find the apostrophe key.
#
# Rules are INDEPENDENT and each has its own probability, so a document gets a realistic mixture
# rather than all-or-nothing. `mangle` reports which rules fired so the rates can be measured.
# ---------------------------------------------------------------------------------------------

from keypad import NEIGHBOURS as _KEYNEAR   # the REAL keypad, derived from the SDK scan matrix
# It is alphabetical and four columns wide, not qwerty: on this device `a` neighbours `b` and `e`.
# corpus/keypad.py carries the layout and gate_keypad.py re-derives it from the header.


def mangle(q, rng, rate=1.0):
    """Roughen a question the way a hurried student would. Returns (text, [rules that fired])."""
    fired = []

    def hit(p):
        return rng.random() < p * rate

    # 1. no apostrophe. The single most common one, and the one that broke device test 1.
    if "'" in q and hit(0.55):
        q = q.replace("'", ""); fired.append("apostrophe")
    # 2. no closing question mark
    if q.endswith("?") and hit(0.40):
        q = q[:-1]; fired.append("noqmark")
    # 3. no trailing period
    if q.endswith(".") and hit(0.35):
        q = q[:-1]; fired.append("noperiod")
    # 4. all lower case, including a proper name
    if any(c.isupper() for c in q) and hit(0.50):
        q = q.lower(); fired.append("lower")
    # 5. "whats" / "hows" / "dont" contractions typed without the apostrophe already covered by 1;
    #    this is the other direction, a space dropped between two short words.
    if hit(0.06):
        m = list(re.finditer(r"(?<=\w) (?=\w)", q))
        if m:
            i = rng.choice(m).start()
            q = q[:i] + q[i + 1:]; fired.append("nospace")
    # 6. a doubled letter
    if hit(0.10):
        idx = [i for i, c in enumerate(q) if c.isalpha()]
        if idx:
            i = rng.choice(idx)
            q = q[:i] + q[i] + q[i:]; fired.append("double")
    # 7. a dropped letter, never from a word shorter than four characters
    if hit(0.14):
        words = [(m.start(), m.group()) for m in re.finditer(r"[A-Za-z]{4,}", q)]
        if words:
            s, w = rng.choice(words)
            j = rng.randrange(1, len(w) - 1)
            q = q[:s + j] + q[s + j + 1:]; fired.append("drop")
    # 8. a neighbouring key
    if hit(0.12):
        idx = [i for i, c in enumerate(q) if c.lower() in _KEYNEAR]
        if idx:
            i = rng.choice(idx)
            r = rng.choice(_KEYNEAR[q[i].lower()])
            q = q[:i] + (r.upper() if q[i].isupper() else r) + q[i + 1:]; fired.append("nearkey")
    # 9. two letters transposed
    if hit(0.08):
        idx = [i for i, c in enumerate(q[:-1]) if c.isalpha() and q[i + 1].isalpha()]
        if idx:
            i = rng.choice(idx)
            q = q[:i] + q[i + 1] + q[i] + q[i + 2:]; fired.append("swap")
    # 10. a trailing space, which the composer keeps
    if hit(0.05):
        q = q + " "; fired.append("trailspace")
    return q, fired


def ask(term, rng, mangle_rate=0.0):
    """One question surface for `term`. mangle_rate scales every typo probability at once."""
    reg = rng.choices(list(WEIGHTS), weights=list(WEIGHTS.values()))[0]
    q = rng.choice(BANKS[reg]).format(t=term)
    if mangle_rate > 0:
        q, _ = mangle(q, rng, mangle_rate)
    return q, reg


def variety(term="Hooke's law", n=2000, seed=0, mangle_rate=0.6):
    """Measure what the bank produces. A frame count is a fact about the file; this is about the
    output, and the two differ the moment a weight is wrong."""
    rng = random.Random(seed)
    seen, regs, fires = {}, {}, {}
    for _ in range(n):
        q, reg = ask(term, rng, mangle_rate)
        seen[q] = seen.get(q, 0) + 1
        regs[reg] = regs.get(reg, 0) + 1
    rng = random.Random(seed + 1)
    for _ in range(n):
        _, f = mangle(rng.choice(ALL_FRAMES).format(t=term), rng, mangle_rate)
        for r in f:
            fires[r] = fires.get(r, 0) + 1
    return {"distinct": len(seen), "n": n,
            "top": sorted(seen.items(), key=lambda kv: -kv[1])[:5],
            "registers": {k: round(v / n, 3) for k, v in sorted(regs.items())},
            "mangle_rates": {k: round(v / n, 3) for k, v in sorted(fires.items())}}


if __name__ == "__main__":
    import json
    print(f"frames: {len(ALL_FRAMES)} ({len(set(ALL_FRAMES))} distinct) across {len(BANKS)} registers")
    for name, b in BANKS.items():
        print(f"   {name:10s} {len(b):3d} frames   weight {WEIGHTS[name]}")
    v = variety()
    print(f"\ndistinct surfaces from {v['n']} draws of one term: {v['distinct']}")
    print(f"register mix : {v['registers']}")
    print(f"mangle rates : {v['mangle_rates']}")
    print("\nsample:")
    rng = random.Random(7)
    for _ in range(18):
        q, reg = ask("Hooke's law", rng, 0.6)
        print(f"   [{reg:9s}] {q}")
