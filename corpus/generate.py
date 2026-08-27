#!/usr/bin/env python3
"""Synthetic document generator + diversity metrics.

Token count is the wrong instrument: 310k documents from 27 record heads is 27 patterns repeated,
and the token count looks identical to a genuinely varied corpus. Everything here is reported
alongside three diversity measures, never alone."""
import json
import sys as _sys, pathlib as _pl
_sys.path.insert(0, str(_pl.Path(__file__).parent))
from atomic import write_json as _wj, write_text as _wt
import json, re, random, subprocess, collections, math, sys, time, pathlib

# Universal constants are NOT free variables. The hand-read of 30 found 8 documents assigning
# random values to h, c, k_e, a_0, epsilon_0 and mu_0 -- teaching that Planck's constant is 36.
# They are supplied by the record (PROMPT_FORMAT section 2) and never sampled.
CONST = {
 "h":6.626e-34, "hbar":1.055e-34, "c":2.998e8, "G":6.674e-11, "k_e":8.988e9, "k":8.988e9,
 "epsilon_0":8.854e-12, "mu_0":1.257e-6, "a_0":5.292e-11, "N_A":6.022e23, "R":8.314,
 "sigma":5.670e-8, "g":9.81, "e":1.602e-19, "m_e":9.109e-31, "m_p":1.673e-27,
}

# ---- CONSTANTS RESOLVE PER RECORD, NOT PER NAME -------------------------------------------------
#
# `CONST[v]` is a global name->meaning table with no scope argument -- the SEVENTH instance of the
# pattern docs/IDENTIFIER_COLLISION.md documents, in the file that produces every training document,
# against a rule that document already states as enforceable: "any function mapping a name to a
# meaning must take a scope argument."
#
# Measured, by replaying this substitution over the record set: 9 of 77 productive heads got a
# DIFFERENT PHYSICAL QUANTITY. h := Planck's constant where the record declares metres of depth.
# R := the molar gas constant where the record declares ohms, and again where it declares a radius.
# c := the speed of light where the record declares J/(kg*K). That is every DC-circuit relation in
# the corpus, all of rolling motion, hydrostatics and calorimetry.
#
# The scope is the RECORD, and it has been carrying the answer all along: store_clean.json's `cval`
# field says which of a relation's symbols are supplied constants and what they are worth. Two rules:
#   1. If the record names the constant in its own cval, use THAT.
#   2. Otherwise fall back to CONST only when the record's DECLARED UNIT is dimensionally the
#      constant's unit. A mismatch means the symbol is a variable that happens to share a name.
# A record with no unit for the symbol gets no substitution -- absence is not permission.
_CUNIT = {"h":"J*s","hbar":"J*s","c":"m/s","G":"N*m^2/kg^2","k_e":"N*m^2/C^2","k":"N*m^2/C^2",
          "epsilon_0":"F/m","mu_0":"T*m/A","a_0":"m","N_A":"1/mol","R":"J/(mol*K)",
          "sigma":"W/(m^2*K^4)","g":"m/s^2","e":"C","m_e":"kg","m_p":"kg"}

def _dim_same(u1, u2):
    """Dimensional equality decided by the SHIPPED evaluator, not by string comparison -- `F/m` and
    `C^2/(N*m^2)` are the same dimension written two ways, and a string rule would reject the
    legitimate epsilon_0 records along with the illegitimate R ones."""
    if not u1 or not u2: return False
    if u1.replace(" ","") == u2.replace(" ",""): return True
    p = subprocess.run(["tools/eval/evalcli", f"<tool>conv<arg>1 {u1}<arg>{u2}</tool>"],
                       capture_output=True, text=True)
    return "!" not in p.stdout

_CONST_CACHE = {}
def _const_for(rec, v):
    """The value of `v` in `rec` if it is a supplied constant there, else None."""
    key = (rec.get("f"), v)
    if key in _CONST_CACHE: return _CONST_CACHE[key]
    out = None
    cv = (_store.get(rec.get("f"), {}) or {}).get("cval") or {}
    if v in cv:
        try: out = float(cv[v])
        except (TypeError, ValueError): out = None
    elif v in CONST:
        u = (rec.get("units") or {}).get(v)
        out = CONST[v] if (u and _dim_same(u, _CUNIT[v])) else None
    _CONST_CACHE[key] = out
    return out

_EMP = json.load(open("corpus/empirical_values.json"))
_POOL = sorted(v for vs in _EMP.values() for v in vs)

def sample_value(rng):
    """Draw from the empirical OpenStax distribution: median ~5, 60-90% round numbers.
    uniform(1.5, 95) produced m = 92.6 kg and r = 70.25 m, which no textbook contains."""
    return rng.choice(_POOL)

VAR = re.compile(r"(?<![A-Za-z0-9_])([A-Za-z][A-Za-z0-9_]*)(?![A-Za-z0-9_(])")
RES = {"pi","e","sin","cos","tan","ln","log","sqrt","exp","d","f","x","t"}

def usable(r):
    """Physics-relation shape. The FIRST version of this filter passed Blv_d=55.0 and
    Dsintheta=mlambda -- fluent documents in which three variables had been collapsed into one
    identifier by the MathML converter. The diversity metrics scored that run as healthy
    (0.62 distinct 4-gram, 95% head coverage), because diversity cannot see semantic garbage.
    These two clauses are what catch it, and they are why a semantic gate exists separately."""
    f = r["f"]
    if "(" in f.split("=")[0]: return False               # f(x)= is a definition, not a relation
    if re.search(r"[A-Za-z_]\d*\s*\*?\(", f): return False  # function application, incl. f*(x)
    lhs, rhs = f.split("=", 1)
    if not re.fullmatch(r"[A-Za-z][A-Za-z0-9_]*", lhs): return False
    vs = {v for v in VAR.findall(rhs)} - {"pi","e"}
    # SEMANTIC GATE: the answer must require arithmetic, not just echo a substituted value,
    # and a relation with one variable teaches nothing about combining quantities.
    if not re.search(r"[+\-*/^]", rhs): return False
    # Prefix operators that the converter cannot distinguish from multiplication: Delta*V is
    # "change in V", not Delta times V; d*x/d*t is a derivative, not four multiplied symbols.
    # 7% of gated records. Dropped rather than shipped wrong -- the physics is silently altered
    # otherwise, and no downstream gate would catch it.
    if re.search(r"\b(Delta|d|partial|Sigma|nabla)\s*\*", f): return False
    return 2 <= len(vs) <= 4 and len(rhs) <= 40

# Only ANNOTATED, dimensionally-gated records enter the corpus. The prompt format signed off in
# PROMPT_FORMAT section 2 is FORMULA | VAR:UNIT ... | CONDITION, and the previous generator emitted
# FORMULA | VAR VAR -- no units, no condition. That made every eval prompt out-of-distribution and
# meant the applicability condition, which licenses every D-category refusal, never appeared in
# training at all.
# TRAINING formulas only. The held-out set is reserved strictly for measuring generalisation
# across unseen formulas and must never reach the corpus.
# ITERATE THE ANNOTATED SET, NOT THE MINED ONE.
#
# This used to read `for r in records_raw if usable(r) and r["f"] in units_train`, which iterates the
# MINED set and merely filters by the annotation store. records_raw holds 377 relations extracted
# from OpenStax "Key Equations" tables; units_train holds 200 hand-annotated with units. The
# intersection is 78, and the 122 relations that were annotated but never mined were unreachable --
# including FIFTEEN OF THE TWENTY the eval set tests. `v=d/t` carries 19 of the 200 eval items and
# had never appeared in a single training document. Measured: 5 of 20 eval heads covered, 21.6% of
# eval items. See docs/RELATION_COVERAGE.md.
#
# The reason mining cannot supply them is structural: Key Equations tables list what a chapter
# DERIVES, and the foundational relations -- F=m*a, v=d/t, K=0.5*m*v^2 -- are assumed and stated
# inline in prose. Re-running the miner over all twelve books yields 0 new relations. Breadth comes
# from mining; the core comes from hand-authoring, and iterating the mined set makes the core
# unreachable by construction.
_ann  = {r["f"]: r for r in json.load(open("corpus/units_train.json"))}
_mined = {r["f"]: r for r in json.load(open("corpus/records_raw.json"))}
# NAMES. units_train's `name` is often the WORKED EXAMPLE'S TITLE rather than the quantity's --
# "Curiosity Rover", "X-Rays from Aluminum", "(from worked example)" -- and generate.py puts that
# straight into the question as "find the {name}". store_clean.json was hand-curated with real
# relation names, so prefer it; 45 of the 82 bad names are covered that way, and all 20 eval
# relations get a proper name with no new work.
_store = {r["f"]: r for r in json.load(open("corpus/store_clean.json"))}


def lhs_unit(r):
    """The unit the ANSWER must carry, read from the store first for the reason A9 documents.

    A10. 81% of answer spans stated a dimensioned result with NO UNIT -- "The kinetic energy is 18."
    That is the model's training target, so it is a correctness defect in the shipped output, not a
    cosmetic one: a student is shown a bare number and every gate passed it because grade.py had no
    unit check and items.json's references are bare ("250", not "250 N/m").

    Dimensionless records render BARE, which is the same ruling as F3 in tools/eval/dispatch.c --
    "0.25", not "0.25 1", because that is what a physicist writes. 8 of 141 records are affected and
    they must not acquire a spurious "1"."""
    lhs = r["f"].split("=", 1)[0].strip()
    u = (_store.get(r.get("f"), {}) or {}).get("units") or r.get("units") or {}
    un = (u.get(lhs) or "").strip()
    return "" if un in ("", "1") else un


# A17. A PHYSICALLY IMPOSSIBLE RESULT IS NOT A TRAINING EXAMPLE, and nothing owned this.
# distribution_gate's tokenizer is `[a-z]+`, so it cannot see digits at all; provenance checks that
# a number was SOURCED, not that it is possible; the shape check compares structure; dim_gate
# checks dimensions, and -28.75 is dimensionally a fine efficiency. Measured 94 of 4,957 documents
# (1.90%): negative times of flight, a refractive index of 3.06e+07, a Carnot efficiency of -28.75,
# g = 4352 m/s^2.
#
# The mechanism is sample_value() drawing each variable independently, so nothing stops T_c > T_h
# or an angle past 90 degrees. Filtering the RESULT rather than constraining every input is the
# narrow fix: it needs one rule per quantity CLASS instead of per relation, and it cannot miss a
# combination nobody thought of.
#
# NARROW BY CONSTRUCTION, and the limit is the honest part: it covers quantity classes that have a
# NAMED physical range. A quantity outside this table is unchecked, not blessed -- most results are,
# and "not in the table" must not read as "verified plausible".
def implausible(r, value):
    """Why this result cannot occur physically, or None. `r` is the record, `value` the number."""
    lhs = r["f"].split("=", 1)[0].strip()
    unit = ((r.get("units") or {}).get(lhs) or "").strip()
    name = (r.get("name") or "").lower()
    if unit == "s" and value <= 0:                    return "a duration must be positive"
    if unit == "kg" and value <= 0:                   return "a mass must be positive"
    # A CHANGE in temperature may be negative; an ABSOLUTE one may not.
    if unit == "K" and not lhs.startswith(("Delta", "delta")) and value <= 0:
        return "an absolute temperature must be positive"
    if "efficiency" in name and not 0.0 <= value <= 1.0:
        return "an efficiency must lie in [0, 1]"
    if "index of refraction" in name and not 1.0 <= value <= 100.0:
        return "a refractive index must lie in [1, 100]"
    if "gravitational acceleration" in name and not 0.0 < value < 100.0:
        return "g must lie in (0, 100) m/s^2"
    return None


def _num(x):
    """Render a given so the tool call's literal is byte-findable in the question.

    `{:g}` is a DISPLAY format: it rounds 1054.804 to "1054.8", and the call then carries the full
    float, so provenance correctly reports a literal the model never saw. repr() round-trips, and
    trailing ".0" is trimmed because "m = 2.0" reads wrong where "m = 2" reads right -- and 2.0 and
    2 are the same float, so the call still matches."""
    r = repr(float(x))
    return r[:-2] if r.endswith(".0") else r


def _condition_field(r):
    """The record span's condition field, by the SHIPPED assembler's rule (assemble.c:94).

    Reads `req` from the STORE, not from the annotated record the generator iterates -- 0 of 141
    of those carry it, which is exactly how the two producers came to disagree on 57.4% of records
    while each looked internally consistent."""
    req = (_store.get(r.get("f"), {}) or {}).get("req") or r.get("req")
    return req if req else "standard conditions"
# Rejects a WORKED-EXAMPLE TITLE used as a relation name ("Using the Monotone Convergence Theorem",
# "Calculating Displacement: A Subway Train"). Every marker names a thing a TITLE does.
#
# Bare `Theorem` was in this list and is not such a marker: it rejected "Work-energy theorem" and
# "impulse-momentum theorem", two legitimate relation names, leaving both records shipped-but-
# untrained. "Contains the word Theorem" was a proxy for "is a worked-example title", and the
# titles it was meant to catch all carry a leading verb -- `Using ` catches the Monotone
# Convergence one on its own.
#
# A name that denotes a RELATION rather than a quantity is handled downstream, not here:
# quantity_surface() sends those to the LHS symbol (A6f), so "Work-energy theorem" produces
# "Calculate W_net", never "Calculate work-energy theorem".
_BADNAME = re.compile(r"^\(|Example|Using |Calculat|^Find |Determin|Problem", re.I)

def _best_name(f, ann):
    for cand in (_store.get(f, {}).get("name"), ann.get("name"), _mined.get(f, {}).get("name")):
        if cand and not _BADNAME.search(cand): return cand
    return None

# R_train SUPSETEQ R_store. THE ITERATION SET IS THE UNION, and it used to be units_train alone --
# so 25 records the device can RETRIEVE were never taught. A record the model has never seen
# produces a well-formed document with the wrong structure at 12.2% against 41.0% for one it has,
# and the picker can return any of the 166. Shipping a relation you did not train on is shipping a
# confident wrong answer with a retrieval path to it.
#
# All 25 carried COMPLETE units and a name in store_clean.json already; nothing had to be annotated,
# the iteration simply never reached them. units_train stays the authority on units WHERE IT HAS
# THEM -- it is the hand-annotated set -- and the store supplies them otherwise.
#
# This closes the rule and stops. Breadth beyond the store (the 377 Gate-A survivors) is a separate
# decision with its own control: it has no measured effect on correctness, and coverage of what
# SHIPS is what 41.0/12.2 measures.
recs, _unnamed, _uncleaned = [], [], []
_keys = list(_ann) + [f for f in _store if f not in _ann]
for f in _keys:
    ann = _ann.get(f) or {}
    src = _mined.get(f) or _store.get(f) or dict(ann)
    r = dict(src); r["f"] = f
    r["units"] = ann.get("units") or (_store.get(f, {}) or {}).get("units") or {}
    if not r["units"]:
        _unnamed.append(f)                     # no units anywhere: cannot state an answer's unit
        continue
    # usable() IS A MINING FILTER. Its two clauses catch converter artefacts -- `f(x)=` definition
    # shapes and MathML fusion like `K*E` where `KE` was meant -- which can only occur in text that
    # came through extract_records.py. Applying it to a HAND-CURATED record is a category error, and
    # an expensive one: it dropped 23 hand-verified store records including E=m*c^2, kinetic energy,
    # drag force, the law of reflection and rms voltage. Its function-application regex,
    # `[A-Za-z_]\d*\s*\*?\(`, matches `m*(v)^(2)` -- an ordinary product -- and `K=0.5*m*(v)^(2)`
    # carries 12 of the 200 eval items.
    #
    # So: a record that appears in store_clean.json has been read by a human and is exempt. A
    # mined-only record is not, and the filter still earns its place there -- of the 4 mined-only
    # drops, one is `K*E=...` (fusion) and one is `U(x)=...` (definition shape).
    if r.get("drop"): continue
    # store_clean.json IS THE AUTHORITY ON LEGITIMACY. It is the OUTPUT of a review that deleted 34
    # records for documented reasons (docs/RESULT_STORE_CLEANING.md): non-physics recurrences,
    # duplicates, and misnamed relations -- `R_eqv=R_1-R_2` (resistances do not subtract),
    # `v=lambda*f` labelled "speed of light" when it is wave speed, `F=k*x` against the signed
    # `F=-k*x`. units_train.json is the authority on UNITS and was never re-cleaned, so it still
    # carries all three.
    #
    # The first version of this change iterated units_train alone and RE-ADMITTED 24 of them,
    # including three named "Strategy", one named "Graficar una ecuacion polar", and two whose name
    # is their own formula. That is the store-cleaning silently undone by a change made two
    # commits later -- the same shape as every other defect in this file's history, and caught only
    # by auditing what had actually landed.
    if f not in _store:
        _uncleaned.append((f, ann.get("name", "")))
        continue
    if not usable(r) and f not in _store: continue
    nm = _best_name(f, ann)
    if not nm:
        # REPORTED, NOT SILENT. usable() already drops 31 records with no output at all, and simple
        # harmonic motion is absent from the corpus because of it. A relation skipped for want of a
        # name is a relation someone can supply a name for in one line, and they can only do that if
        # the generator says which.
        _unnamed.append(f); continue
    r["name"] = nm
    recs.append(r)

# Applicability conditions, drafted per subject. THIN by design and flagged as such: these are the
# weakest annotation in the pipeline and the one the refusal metrics lean on hardest.
COND = [
 (r"roll|rotat|angular|torque|moment of inertia", "rigid body, fixed axis"),
 (r"mirror|lens|optic|focal|magnif|refract",      "thin lens or spherical mirror, paraxial rays"),
 (r"drag|fluid|buoyan|viscos|flow",               "steady flow, constant density"),
 (r"circuit|capacit|induct|resist|current|ohm",   "steady state, ohmic components"),
 (r"photon|quantum|debroglie|planck|bohr",        "non-relativistic, single particle"),
 (r"therm|heat|entrop|gas|carnot|molar",          "quasi-static, no phase change"),
 (r"spring|hooke|oscillat|harmonic",              "within the elastic limit, no damping"),
 (r"magnetic|solenoid|hall|lorentz|flux",         "uniform field, steady current"),
 (r"free fall|projectile|kinemat|velocity|accel", "constant acceleration, no air resistance"),
 (r"relativ|doppler|lorentz factor",              "inertial frames, constant relative velocity"),
]
def condition(name):
    n = name.lower()
    for pat, c in COND:
        if re.search(pat, n): return c
    return "standard conditions"

_NOT_A_HEAD = {"of", "in", "on", "for", "to", "a", "an", "the", "and", "or", "with",
               "from", "by", "at", "per", "into", "under", "over", "as", "its"}

# A NAME THAT DENOTES A RELATION IS NOT A NAME FOR ITS OUTPUT. 24 of 141 records are named after
# the equation rather than the quantity it produces -- "law of reflection", "transformer equation",
# "common expression of ohm's law", "relationship between frequency and period" -- and asking
# "Compute law of reflection." is asking for the wrong kind of thing. For these the only honest
# surface is the LHS symbol. Found by hand-reading 30 questions after the frame fix: 4 of 30 were
# of exactly this shape and nothing else explained them.
# A14. Extended: a name denoting a PROCESS, FUNCTION or EFFECT is no more a name for a quantity
# than a law is. "energy-mass conversion", "Potential-energy function of a harmonic oscillator" and
# "Relativistic Doppler effect for frequency" all produced asks for the wrong kind of thing --
# 128 of 11,975 documents. Same rule as the law/theorem case: send them to the LHS symbol.
_RELATION_NAME = re.compile(r"\b(law|equation|expression|relationship|relation|theorem|"
                            r"principle|rule|formula|identity|definition|"
                            r"conversion|function|effect|process)\b", re.I)

# A15. A BARE PROPERTY NOUN IS NOT A QUANTITY. Head extraction turned "Index of refraction" into
# "index", "Magnitude of magnetic force" into "magnitude" and "change in Fahrenheit temperature"
# into "change", giving "Find index." and "Determine magnitude." -- 191 of 11,975 documents asking
# for nothing identifiable. These words are the PROPERTY, and the thing they are a property OF is
# what got dropped. When the head reduces to one of them, keep the full name instead.
_NOT_A_QUANTITY = {"magnitude", "index", "change", "difference", "value", "component",
                   "ratio", "number", "amount", "factor", "rate", "size", "amount"}

def quantity_surface(r, rng):
    """How the QUESTION refers to the quantity being asked for.

    A1. This was `r["name"].lower()` -- the record's canonical name, interpolated verbatim into
    every question, so a training question named its own record and an eval question never did.
    docs/UNENFORCED_REVIEW.md A1.

    IT CANNOT SIMPLY BE REMOVED. "Find the ___" is unanswerable; a question has to say what it
    wants. What was wrong was using ONE surface form, always, and that form being the exact string
    the retrieval record carries. So: vary it, from material the record already has.

    Four surfaces, and none is invented physics:
      the canonical name            "the gravitational potential energy"
      the head noun                 "the potential energy"   (last two words)
      the bare head                 "the energy"
      the symbol                    "U"
    The last two are what a textbook actually writes once the quantity is in play, and they are what
    the eval set writes. Weighted toward the shorter forms because the corpus was 100% canonical.

    MEASURED, and stated because it is small: this is worth ~2 pp of the separability that
    distribution_gate reports, not the 34 pp the gate is above its floor. See RESULT_SEPARABILITY.md
    -- the dominant cause is that the generator's ENTIRE question vocabulary is 249 words."""
    name = (r.get("name") or "").lower().strip()
    lhs  = r["f"].split("=", 1)[0].strip()
    words = name.split()
    forms, weights = [], []

    def _add(form, w):
        """A truncation is only a noun phrase if it BEGINS like one.

        The first version of this took the last two words unconditionally, which turned
        "index of refraction" into "of refraction" and produced "Calculate of refraction."
        A suffix of a noun phrase is not itself a noun phrase, and taking one is a proxy for
        head-noun extraction rather than the thing itself."""
        form = form.strip()
        if not form or form.split()[0] in _NOT_A_HEAD:
            return
        if form.lower() in _NOT_A_QUANTITY:      # A15: names the property, not the quantity
            return
        forms.append(form); weights.append(w)

    if name and _RELATION_NAME.search(name):
        # The name describes the equation, not its output. Only the symbol is usable.
        return lhs or name
    if name:
        _add(name, 2)
        # THE HEAD IS ON THE LEFT OF A PREPOSITION, NOT AT THE END. "acceleration of two blocks
        # connected over a pulley" has head "acceleration"; the last word is "pulley", and taking
        # it produced "what was pulley?". Likewise "speed of sound" -> "sound", "distance to
        # screen" -> "screen", "heat capacity at constant pressure" -> "constant pressure".
        # Position was a proxy for headedness and it is wrong for every "X of Y" name, which is
        # most of them.
        head = re.split(r"\s+(?:of|from|in|on|at|for|to|over|due|between|with|per)\s+", name)[0]
        if head and head != name: _add(head, 3)
    if lhs and len(lhs) <= 6:
        _add(lhs, 2)
    if not forms:
        return name or lhs or "the value"
    return rng.choices(forms, weights=weights, k=1)[0]


def units_field(r):
    """The record span's units field, for ONE record. Exported because the parity gate calls it.

    An earlier version of tools/eval/gate_format_parity.py RE-IMPLEMENTED this rule instead of
    calling it, so mutating the generator did not move the gate and the negative control passed
    while the fix was reverted. A gate that carries its own copy of the rule tests the copy. Same
    lesson as tools/eval/genloop.py: when two places need one behaviour, the second one must call
    the first.

    THE LHS UNIT IS PART OF THE SPAN. `vs` comes from the RIGHT-hand side only, so the variable being
    solved for never got a unit here -- 0 of 197,428 shipped training documents carried it, while
    src/store/assemble.c has emitted it on EVERY device prompt since it was written ("units for
    every variable, LHS included -- the answer needs a unit to state") and test_assemble.c asserts
    it. So every prompt the device produced differed from every document the model trained on, in
    the first field after the formula. docs/EXPERIMENT_PLAN.md:295-313 records this exact skew as
    found and fixed -- "the five-field skeleton was unified". The skeleton was; the units were not.
    """
    u = r.get("units") or {}
    lhs = r["f"].split("=", 1)[0].strip()
    vs = sorted({v for v in VAR.findall(r["f"].split("=", 1)[1])} - {"pi", "e"})
    order = ([lhs] if lhs in u else []) + [v for v in vs if v != lhs]
    return " ".join(f"{v}:{u[v]}" for v in order if v in u)

for r in recs: r["cond"] = condition(r["name"])
print(f"records usable as physics relations: {len(recs)} of {len(_ann)} annotated "
      f"({len(_unnamed)} skipped for want of a relation name, "
      f"{len(_ann)-len(recs)-len(_unnamed)} by usable(), which exempts hand-curated records)")
if _uncleaned:
    print(f"  SKIPPED, NOT IN THE CLEANED STORE ({len(_uncleaned)}) -- units_train was never re-cleaned")
    print( "  after docs/RESULT_STORE_CLEANING.md deleted 34 records. Each needs a decision:")
    for _f, _n in _uncleaned[:10]: print(f"    {_f:34} {_n[:40]}")
    if len(_uncleaned) > 10: print(f"    ... and {len(_uncleaned)-10} more")
if _unnamed:
    print("  SKIPPED FOR WANT OF A NAME -- add one to corpus/store_clean.json to enable:")
    for _f in _unnamed[:12]: print(f"    {_f}")
    if len(_unnamed) > 12: print(f"    ... and {len(_unnamed)-12} more")

# ---- phrasing templates. Style varies, facts do not. -------------------------
# ASK templates come from the DEVELOPMENT population -- real OpenStax question openings, mined in
# corpus/stems.py. The eval set is NEVER used to select templates: it is the only held-out phrasing
# distribution we have, and tuning against it would destroy the honest read on whether variety
# closes the 25pp generator-vs-eval gap.
ASK = json.load(open("corpus/asks_dev.json"))

# A6. THE MINED ASK TEMPLATES ARE 8 USABLE FRAMES AND 36 FRAGMENTS, and {q} was substituted into
# all 44 blind. Two defects, one cause, both measured on output rather than argued:
#
#   1. WRONG QUANTITY. 16 templates name a quantity of their own, so the question asked for one
#      thing and the answer span supplied another. 4,507 of 20,000 shipped questions -- 22.5%.
#          "How much heat did velocity?"              record v=v_0+a*t
#          "How much work does tangential speed?"     record v=r*omega
#      That is wrong supervision: a quarter of the corpus taught that the quantity named in the
#      question is not the one to answer with.
#
#   2. WRONG FRAME. 36 of 44 templates are sentence fragments mined mid-question, whose complement
#      must be something other than a bare noun phrase. Hand-read of 30 generated questions:
#      19 ill-posed, 63% [Wilson 95% CI 46-78].
#          "Determine whether v_t."          needs a proposition
#          "How long will it take centripetal acceleration?"   needs a duration
#          "Calculate be/ F_net."            mining garbage, literally "be/"
#          "What is his displacement vector d -> k?"           mining garbage
#          "Determine (a) mass."             "(a)" leaked from multi-part exercise numbering
#
# WHAT MISSED BOTH. Every automated check passed these documents -- well-formed, call executes,
# result matches, provenance clean, shape OK, dimensional gate clean, diversity fine. Nothing
# compared the question's subject to the answer's, and nothing read a question. They were found by
# READING ONE, then thirty.
#
# THE FIX IS A HAND-REVIEWED ALLOWLIST, NOT A REGEX. A predicate like "does the template end in
# {q}?" would be a proxy for "does this frame accept a bare noun phrase", and this repo has been
# bitten six times by exactly that substitution. 44 items is small enough to read, so they were
# read, and the eight that survive are named here with the reviewer's claim attached.
#
# THE UNCOMFORTABLE PART, and it belongs in the plan rather than in a comment: mining question
# surfaces yielded EIGHT generic verbs that could have been hand-written in a minute. The template
# "variety" the corpus was credited with was 36 broken frames. This is independent evidence for the
# same conclusion the stem work reached -- mined OpenStax question surface does not transfer.
ASK_REVIEWED = [                      # every frame below takes a bare noun phrase as its object
    "What is {q}?", "Find {q}.", "Calculate {q}.", "Determine {q}.",
    "What was {q}?", "Estimate {q}.", "Compute {q}.",
    # "What are {q}?" is REJECTED despite being a clean frame: quantity_surface() yields singular
    # noun phrases, so it produced "what are spherical mirror?" and "what are motionally induced
    # emf?". A frame is only usable with the surfaces this generator actually emits.
]
_ASK_SHA = "2d0d3b8f"                 # guard: see tools/eval/gate_ask_quantity.py

ASK_ALL = json.load(open("corpus/asks_dev.json"))
ASK = [t for t in ASK_REVIEWED if t in ASK_ALL]
assert len(ASK) == len(ASK_REVIEWED), (
    "a reviewed ASK frame is no longer in corpus/asks_dev.json. The allowlist is a claim about "
    "text that has been READ; it cannot survive that text changing underneath it. Re-read the "
    "file and update ASK_REVIEWED deliberately.")

QUANTITY = re.compile(r"\b(work|heat|speed|velocity|displacement|force|power|energy|time|charge|"
                      r"current|mass|distance|pressure|temperature|acceleration|momentum)\b", re.I)

def _named_quantities(text):
    return {w.lower() for w in QUANTITY.findall(text or "")}

# Retained so the gate can assert the REJECTED frames stay rejected: a gate that only checks the
# survivors cannot notice the fragments coming back.
ASK_REJECTED = [t for t in ASK_ALL if t not in ASK_REVIEWED]

def ask_for(r, surface, rng):
    """Choose a question frame. Only reviewed frames are eligible, so a frame cannot contradict
    the quantity being asked for -- none of them names a quantity at all."""
    return rng.choice(ASK).format(q=surface)

GIVE = ["Given {g}, ", "With {g}, ", "If {g}, ", "For {g}, ", "Where {g}, ",
        "Suppose {g}. ", "Take {g}. ", "A system has {g}. ", "Assume {g}. ",
        "Consider a case where {g}. ", "In a setup with {g}, ", "Measurements give {g}. ",
        "You are told {g}. ", "The values are {g}. ", "Starting from {g}, ", "Using {g}, "]
# A16. `The {q} is ...` interpolates the record's NAME as the computed quantity, so a record named
# for a law produced "The law of reflection is 0.5 rad." -- 232 of 11,975 answers naming a law,
# effect or function as though it were the value. quantity_surface() already refuses those for the
# QUESTION (A6f/A14); the answer side had no such guard, which is the two-implementations pattern
# again. CLOSE_Q is used only when the record's name really denotes a quantity.
CLOSE_ANY = ["{v} = {a}. {why}", "{a}. {why}", "That gives {a}. {why}"]
CLOSE_Q   = CLOSE_ANY + ["The {q} is {a}. {why}"]
WHY = ["Substituting into {f}.", "Directly from {f}.", "From {f}.", "Using {f}.",
       "This follows from {f}.", "{f} gives it."]

def gen(n, seed=0):
    rng = random.Random(seed)
    docs, calls = [], []
    for i in range(n):
        r = rng.choice(recs)
        lhs, rhs = r["f"].split("=", 1)
        vs = sorted({v for v in VAR.findall(rhs)} - {"pi", "e"})
        vals = {v: (_const_for(r, v) if _const_for(r, v) is not None else sample_value(rng)) for v in vs}
        expr = VAR.sub(lambda m: f"({vals[m.group(1)]})" if m.group(1) in vals else m.group(1), rhs)
        free = [v for v in vs if _const_for(r, v) is None]
        if not free: continue                          # nothing left to ask about

        # A7. CONSTANTS ARE GIVENS, and leaving them out taught the model to fabricate numbers.
        #
        # 27 of 141 records carry a resolved constant (g, c, h, G, k_e, R, k_B, eps_0, mu_0). The
        # value went into the TOOL CALL and into neither the question nor the five-field record, so
        # 16.2% of documents (measured, 4,000-doc sample) emit a literal that appears NOWHERE in the
        # model's context. That is the architecture inverted: TOOL_SPEC exists so the model reads
        # numbers rather than recalling them, and a sixth of the corpus trained it to recall one.
        #
        # AND IT MADE THE DEFECT UNGRADEABLE, which is why it survived. Measured on grade.py:
        #
        #   constant absent (as shipped)  g=9.81  -> prov False, shape UNCHECKED
        #   constant absent (as shipped)  g=9810  -> prov False, shape UNCHECKED   <- identical
        #   constant inlined              g=9.81  -> prov True,  shape ok
        #   constant inlined              g=9810  -> prov False, shape MISMATCH
        #
        # Correct and fabricated were indistinguishable to every grader in the repo. grade.py's own
        # docstring says the shape check exists for this case -- "the mgh case proves it" -- and it
        # degraded to `unchecked` on exactly the mgh case. Same shape as dim_gate returning
        # "unknown": "cannot check" scored as "nothing wrong".
        #
        # docs/PROMPT_FORMAT.md section 2 already mandates inlining, build/asmcli already does it on
        # the device path, and tools/eval/items.json A1-004 does it. The generator did not, so the
        # corpus disagreed with the runtime that consumes it.
        consts = [v for v in vs if _const_for(r, v) is not None]
        shown  = free + consts
        # A13. FULL PRECISION IN THE GIVENS. `{:g}` renders 1054.804 as "1054.8" while the call
        # uses the full float, so the literal in <arg> appeared nowhere in the model's context --
        # the A7 defect again, 2 of 10,266 calls, arriving through DISPLAY ROUNDING rather than
        # through an omitted constant. repr() round-trips.
        g = ", ".join(f"{v} = {_num(vals[v])}" for v in shown)
        # Decide AFTER the question exists, not before. Three refusal kinds from this path:
        #   withhold -> a required given is absent   (D1)  missing:X | ... | fit:high
        #   mismatch -> the record does not fit      (D2)  missing:none | ... | fit:high
        #   nomatch  -> the picker found nothing     (D3)  none | missing:none | ... | fit:low
        #
        # A11. THE FIT LABELS NOW MATCH THE DEVICE, AND D2's DID NOT.
        #
        # src/store/assemble.c has exactly two shapes and no others:
        #     line  99   a real record   -> ALWAYS " | fit:high"
        #     line 109   no match        -> "none | missing:none | no matching relation | fit:low"
        #
        # D2 emitted a REAL RECORD with fit:low -- a shape the runtime can never produce. 616 of
        # 11,975 documents (5.14%) were in it, and ZERO were in the shape the device actually emits
        # when the picker finds nothing. So the corpus taught a prompt the model will never see and
        # never taught the one it will.
        #
        # And the fix is not cosmetic. On the device a BAD match still arrives as fit:high with a
        # real record, because ns_assemble does not judge fit -- the picker just returns its best
        # candidate. So D2 must be fit:high: the model has to refuse from the record's CONTENT not
        # matching the question, which is the judgement, rather than from a label. That is the A8
        # lesson one level deeper -- there the leak was a formatting bit, here it was the fit token
        # itself, and a model trained on the old labels could refuse correctly on every D2 case
        # while having learned nothing about fit.
        #
        # gate_format_parity could not see this: it compares the generator's record span against
        # asmcli FOR A GIVEN RECORD, and the no-match case has no record to compare.
        roll = rng.random()
        withhold = roll < 0.10 and len(vs) >= 2
        mismatch = 0.10 <= roll < 0.15
        nomatch  = 0.15 <= roll < 0.18
        if withhold:
            # A12. WITHHOLD A FREE VARIABLE, NEVER A CONSTANT. `rng.choice(vs)` could pick g, c, h
            # or G -- values the device ALWAYS inlines (A7) -- so 111 of 11,975 documents refused
            # for want of a number the runtime would have supplied. A refusal that fires on a
            # satisfied precondition is wrong supervision: it teaches the model to refuse a
            # question it can answer.
            drop = rng.choice(free) if free else rng.choice(vs)
            shown_w = [v for v in shown if v != drop]
            g = ", ".join(f"{v} = {_num(vals[v])}" for v in shown_w) if shown_w else g
        stem = rng.choice(GIVE).format(g=g)
        ask  = ask_for(r, quantity_surface(r, rng), rng)
        # Vary the ORDER as well as the wording -- givens-first and ask-first are both common in
        # real problems, and ordering moves 4-gram diversity more than the verb does.
        if rng.random() < 0.35:
            q = ask.rstrip(".?") + ("?" if ask.rstrip().endswith("?") else ".") + " " + \
                stem.strip().rstrip(",").rstrip(".") + "."
        else:
            q = stem + (ask[0].lower() + ask[1:] if stem.endswith(", ") else ask)
        umap = units_field(r)
        # ABSENCE MADE EXPLICIT. The negative existential -- "no value exists for this symbol" --
        # becomes a token lookup, the same move as fit for D2 and the tool call for arithmetic.
        miss = drop if withhold else "none"
        band = "high"          # A11: assemble.c:99 -- a real record is ALWAYS fit:high
        # A18. THE MISMATCH RECORD MUST ACTUALLY NOT FIT. `rng.choice(recs)` can draw a record
        # that computes the very quantity asked for -- 2 of 11,975 documents refused while showing
        # a record that answers the question, which is wrong supervision in the direction that
        # teaches over-refusal. Reject any candidate sharing the true record's LHS, and fall back
        # to the unfiltered draw only if the store somehow offers no alternative.
        rec_r = r
        if mismatch:
            _lhs = r["f"].split("=", 1)[0].strip()
            _alt = [x for x in recs if x["f"].split("=", 1)[0].strip() != _lhs]
            rec_r = rng.choice(_alt) if _alt else rng.choice(recs)
        if mismatch:
            # A8. CALL units_field(), do not re-derive it. This branch carried its own copy that
            # read the RHS only, so the LHS symbol never got a unit -- and units_field()'s own
            # docstring records that exact defect being found and fixed. The fix reached one of the
            # two implementations. Third instance of the pattern, after the loss mask and genloop.
            #
            # The cost was not cosmetic. It made fit:low PERFECTLY PREDICTABLE from a formatting
            # bit: measured over 6,000 documents, the LHS unit was absent from 331/331 = 100.0% of
            # fit:low records and 42/5,669 = 0.7% of fit:high. A one-bit classifier separates the
            # refusal classes, so a model can learn "no LHS unit -> say it does not apply" without
            # ever weighing whether the record fits, and every D2 refusal metric would be measuring
            # the cue rather than the judgement. Same shape as the topic-scoping artefact: a number
            # that looks like a capability and is a property of the format.
            umap = units_field(rec_r)
        docs.append({"q": q, "withhold": drop if withhold else None, "nomatch": nomatch,
                     "mismatch": (rec_r.get("display") or rec_r.get("name","that quantity")) if mismatch else None,
                     # A9. THE CONDITION FIELD MUST MATCH THE SHIPPED ASSEMBLER, and it did not
                     # on 81 of 141 records (57.4%). src/store/assemble.c:94 is the authority:
                     #     r->req && r->req[0] ? r->req : "standard conditions"
                     # The generator instead used condition(name), a name-derived lookup, and only
                     # 2 of 141 store records carry `req` at all -- so the device says "standard
                     # conditions" where the model trained on "constant acceleration, no air
                     # resistance". A train/serve skew in the fourth field of every prompt.
                     #
                     # The name-derived conditions are arguably more informative, and that is not
                     # the deciding question: the model must be trained on what the device emits.
                     # Changing the device instead would be the other fix and it ships in C on
                     # hardware; if the richer conditions are wanted, they belong in the store's
                     # `req`, where BOTH producers already read them.
                     "rec": f"{rec_r['f']} | {umap} | missing:{miss} | "
                            f"{_condition_field(rec_r)} | fit:{band}", "lhs": lhs,
                     "name": r["name"], "head": r["f"], "unit": lhs_unit(r),
                     "close": rng.choice(CLOSE_Q if not _RELATION_NAME.search(r.get("name") or "") else CLOSE_ANY), "why": rng.choice(WHY).format(f=r["f"])})
        calls.append(f"<tool>eval<arg>{expr}</tool>")
    out = re.findall(r"<res>(.*?)</res>",
          subprocess.run(["tools/eval/evalcli","-"], input="\n".join(calls)+"\n",
                         capture_output=True, text=True).stdout, re.S)
    built, dropped = [], 0
    for d, c, res in zip(docs, calls, out):
        if d.get("nomatch"):
            # D3 -- THE SHAPE THE DEVICE EMITS WHEN THE PICKER FINDS NOTHING, and the corpus had
            # ZERO of them. Byte-identical to ns_assemble_none (src/store/assemble.c:109), which is
            # why the literal is written out here rather than composed from the five-field builder:
            # this prompt has no record, so it has no units, no condition and no formula, and
            # composing it would invite the fields back in.
            ans = "I cannot answer that — no record matches this question."
            built.append({"head": d["head"], "kind": "D3", "ans": ans,
                          "text": f"<q>{d['q']}</q><r>none | missing:none | "
                                  f"no matching relation | fit:low<a>{ans}<end>"})
            continue
        if d.get("mismatch"):
            ans=f"I cannot answer that — the record gives {d['mismatch'].lower()}, which does not apply."
            built.append({"head": d["head"], "kind": "D2",
                          "text": f"<q>{d['q']}</q><r>{d['rec']}<a>{ans}<end>", "ans": ans})
            continue
        if d.get("withhold"):
            # Same question, same vocabulary, same record -- one given absent and a refusal answer.
            ans = f"I cannot answer that — {d['withhold']} is not given."
            built.append({"head": d["head"], "kind": "D1",
                          "text": f"<q>{d['q']}</q><r>{d['rec']}<a>{ans}<end>", "ans": ans})
            continue
        if res.startswith("!"): dropped += 1; continue        # TOOL_SPEC 8.1: drop, never guess
        _rec = next((x for x in recs if x["f"] == d["head"]), None)
        if _rec is not None:
            try:    _why = implausible(_rec, float(str(res).split()[0]))
            except (ValueError, IndexError): _why = None
            if _why:
                dropped += 1; continue                # A17: drop, do not teach an impossible value
        try:   res = f"{float(res):.4g}"            # signed off: 4 significant figures
        except ValueError: pass
        a_txt = f"{res} {d['unit']}".strip() if d.get("unit") else res
        ans = d["close"].format(v=d["lhs"], a=a_txt, q=d["name"].lower(), why=d["why"])
        built.append({"head": d["head"],
                      "text": f"<q>{d['q']}</q><r>{d['rec']}{c}<res>{res}</res><a>{ans}<end>",
                      "ans": ans})
    return built, dropped

# ---- diversity metrics -------------------------------------------------------
def ngrams(s, n=4):
    w = s.split()
    return [tuple(w[i:i+n]) for i in range(max(0, len(w)-n+1))]

def diversity_by_span(docs):
    """Per-span diversity. The composite ratio averages over spans with DIFFERENT ROLES and is
    therefore the wrong instrument: the record span is a canonical retrieval record and is SUPPOSED
    to repeat -- it carries 25% of the corpus's 4-grams at a ratio of 0.0002, dragging the composite
    down for a reason that is not a defect. Question and answer diversity are what the model must
    learn from; record diversity is bounded by head count and nothing else."""
    def ng(s, n=4):
        w = s.split(); return [tuple(w[i:i+n]) for i in range(max(0, len(w)-n+1))]
    out = {}
    for lab, get in (("question", lambda d: d["text"].split("<q>")[1].split("</q>")[0]),
                     ("record",   lambda d: d["text"].split("<r>")[1].split("<tool>")[0]),
                     ("answer",   lambda d: d["ans"])):
        gs = [g for d in docs for g in ng(get(d))]
        out[lab] = len(set(gs)) / max(1, len(gs))
    return out

def diversity(docs):
    allg = [g for d in docs for g in ngrams(d["text"])]
    heads = collections.Counter(d["head"] for d in docs)
    # phrasing entropy per head: how many distinct answer shapes each head appears in
    per = collections.defaultdict(set)
    for d in docs: per[d["head"]].add(re.sub(r"[-\d.]+", "#", d["ans"]))
    ent = []
    for h, shapes in per.items():
        c = collections.Counter()
        for d in docs:
            if d["head"] == h: c[re.sub(r"[-\d.]+", "#", d["ans"])] += 1
        tot = sum(c.values())
        ent.append(-sum((v/tot)*math.log2(v/tot) for v in c.values()) if tot > 1 else 0.0)
    return {"distinct_4gram_ratio": len(set(allg))/max(1, len(allg)),
            "head_coverage": len(heads)/len(recs),
            "heads_used": len(heads),
            "mean_phrasing_entropy_bits": sum(ent)/max(1, len(ent))}

if __name__ == "__main__":
    N = int(sys.argv[1]) if len(sys.argv) > 1 else 10000
    t0 = time.time(); docs, dropped = gen(N, seed=20260820); el = time.time()-t0
    chars = sum(len(d["text"]) for d in docs)
    D = diversity(docs)
    print(f"\ngenerated {len(docs):,} documents ({dropped} dropped on evaluator error) in {el:.1f}s")
    print(f"  THROUGHPUT      {len(docs)/el:,.0f} docs/s   ->  185M tokens in "
          f"{185e6*4.15/(chars/len(docs))/(len(docs)/el)/3600:.2f} h")
    print(f"  chars/doc       {chars/len(docs):.0f}")
    print(f"  tokens @4.15    {chars/4.15/1e6:.2f}M from this run")
    print(f"\n  DIVERSITY  (never report the token count without these)")
    print(f"    composite (RETIRED)          {D['distinct_4gram_ratio']:.4f} @{len(docs):,} docs")
    print(f"    head coverage                {D['head_coverage']*100:.1f}%  ({D['heads_used']} heads)")
    print(f"    mean phrasing entropy        {D['mean_phrasing_entropy_bits']:.2f} bits/head")
    SP = diversity_by_span(docs)
    # Corpus size is stamped on every diversity figure: these ratios fall with document count, so
    # a figure quoted without its N is not comparable to any other. I once compared a 200k run
    # against 100k arm figures and read a real gain as a loss.
    print(f"    per-span 4-gram @{len(docs):,} docs  question {SP['question']:.4f}   "
          f"record {SP['record']:.4f}   answer {SP['answer']:.4f}")
    print(f"      (the composite above is dragged down by the record span, which is a canonical")
    print(f"       retrieval record and is SUPPOSED to repeat -- bounded by head count alone)")
    D.update({"span_"+k: v for k, v in SP.items()})
    _wt("corpus/synth_sample.jsonl", 
        "\n".join(json.dumps(d) for d in docs))
    _wj("corpus/diversity.json", D, indent=1)
    print("\n  sample:"); [print("   ", d["text"][:150]) for d in docs[:3]]
