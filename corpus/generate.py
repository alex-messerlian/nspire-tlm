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
_BADNAME = re.compile(r"^\(|Example|Using |Calculat|^Find |Determin|Problem|Theorem", re.I)

def _best_name(f, ann):
    for cand in (_store.get(f, {}).get("name"), ann.get("name"), _mined.get(f, {}).get("name")):
        if cand and not _BADNAME.search(cand): return cand
    return None

recs, _unnamed, _uncleaned = [], [], []
for f, ann in _ann.items():
    src = _mined.get(f) or _store.get(f) or dict(ann)
    r = dict(src); r["f"] = f; r["units"] = ann["units"]
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
    if name:
        forms.append(name);                       weights.append(2)
        if len(words) >= 2:
            forms.append(" ".join(words[-2:]));   weights.append(3)
        if len(words) >= 3:
            forms.append(words[-1]);              weights.append(2)
    if lhs and len(lhs) <= 6:
        forms.append(lhs);                        weights.append(2)
    if not forms:
        return lhs or "the value"
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
GIVE = ["Given {g}, ", "With {g}, ", "If {g}, ", "For {g}, ", "Where {g}, ",
        "Suppose {g}. ", "Take {g}. ", "A system has {g}. ", "Assume {g}. ",
        "Consider a case where {g}. ", "In a setup with {g}, ", "Measurements give {g}. ",
        "You are told {g}. ", "The values are {g}. ", "Starting from {g}, ", "Using {g}, "]
CLOSE = ["{v} = {a}. {why}", "The {q} is {a}. {why}", "{a}. {why}", "That gives {a}. {why}"]
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
        g = ", ".join(f"{v} = {vals[v]:g}" for v in free)
        # Decide AFTER the question exists, not before. Two refusal kinds, both from this path:
        #   withhold -> a required given is absent      (D1, missing:X, fit:high)
        #   mismatch -> the record does not fit         (D2, missing:none, fit:low)
        # D2 was silently lost when the separate refusal.py path was disabled for one-path, and the
        # checkpoint that produced a 50% zero-shot refusal on fit:low had never seen the token.
        roll = rng.random()
        withhold = roll < 0.10 and len(vs) >= 2
        mismatch = 0.10 <= roll < 0.15
        if withhold:
            drop = rng.choice(vs)
            free_w = [v for v in free if v != drop]
            g = ", ".join(f"{v} = {vals[v]:g}" for v in free_w) if free_w else g
        stem = rng.choice(GIVE).format(g=g)
        ask  = rng.choice(ASK).format(q=quantity_surface(r, rng))
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
        band = "low" if mismatch else "high"
        rec_r = rng.choice(recs) if mismatch else r
        if mismatch:
            _vs=sorted({v for v in VAR.findall(rec_r["f"].split("=",1)[1]) if v not in RES})
            umap=" ".join(f"{v}:{rec_r['units'][v]}" for v in _vs if v in rec_r.get("units",{}))
        docs.append({"q": q, "withhold": drop if withhold else None,
                     "mismatch": (rec_r.get("display") or rec_r.get("name","that quantity")) if mismatch else None,
                     "rec": f"{rec_r['f']} | {umap} | missing:{miss} | "
                            f"{rec_r.get('req', r['cond'])} | fit:{band}", "lhs": lhs,
                     "name": r["name"], "head": r["f"],
                     "close": rng.choice(CLOSE), "why": rng.choice(WHY).format(f=r["f"])})
        calls.append(f"<tool>eval<arg>{expr}</tool>")
    out = re.findall(r"<res>(.*?)</res>",
          subprocess.run(["tools/eval/evalcli","-"], input="\n".join(calls)+"\n",
                         capture_output=True, text=True).stdout, re.S)
    built, dropped = [], 0
    for d, c, res in zip(docs, calls, out):
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
        try:   res = f"{float(res):.4g}"            # signed off: 4 significant figures
        except ValueError: pass
        ans = d["close"].format(v=d["lhs"], a=res, q=d["name"].lower(), why=d["why"])
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
