#!/usr/bin/env python3
"""Synthetic document generator + diversity metrics.

Token count is the wrong instrument: 310k documents from 27 record heads is 27 patterns repeated,
and the token count looks identical to a genuinely varied corpus. Everything here is reported
alongside three diversity measures, never alone."""
import json, re, random, subprocess, collections, math, sys, time, pathlib

# Universal constants are NOT free variables. The hand-read of 30 found 8 documents assigning
# random values to h, c, k_e, a_0, epsilon_0 and mu_0 -- teaching that Planck's constant is 36.
# They are supplied by the record (PROMPT_FORMAT section 2) and never sampled.
CONST = {
 "h":6.626e-34, "hbar":1.055e-34, "c":2.998e8, "G":6.674e-11, "k_e":8.988e9, "k":8.988e9,
 "epsilon_0":8.854e-12, "mu_0":1.257e-6, "a_0":5.292e-11, "N_A":6.022e23, "R":8.314,
 "sigma":5.670e-8, "g":9.81, "e":1.602e-19, "m_e":9.109e-31, "m_p":1.673e-27,
}
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
_ann = {r["f"]: r for r in json.load(open("corpus/units_train.json"))}
recs = [r for r in json.load(open("corpus/records_raw.json"))
        if usable(r) and r["f"] in _ann and not r.get("drop")]
for r in recs: r["units"] = _ann[r["f"]]["units"]

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
for r in recs: r["cond"] = condition(r["name"])
print(f"records usable as physics relations: {len(recs)} of 379 gated  "
      f"(vs 27 heads in the eval set = {len(recs)/27:.1f}x)")

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
        vals = {v: (CONST[v] if v in CONST else sample_value(rng)) for v in vs}
        expr = VAR.sub(lambda m: f"({vals[m.group(1)]})" if m.group(1) in vals else m.group(1), rhs)
        free = [v for v in vs if v not in CONST]
        if not free: continue                          # nothing left to ask about
        g = ", ".join(f"{v} = {vals[v]:g}" for v in free)
        stem = rng.choice(GIVE).format(g=g)
        ask  = rng.choice(ASK).format(q=r["name"].lower())
        # Vary the ORDER as well as the wording -- givens-first and ask-first are both common in
        # real problems, and ordering moves 4-gram diversity more than the verb does.
        if rng.random() < 0.35:
            q = ask.rstrip(".?") + ("?" if ask.rstrip().endswith("?") else ".") + " " + \
                stem.strip().rstrip(",").rstrip(".") + "."
        else:
            q = stem + (ask[0].lower() + ask[1:] if stem.endswith(", ") else ask)
        umap = " ".join(f"{v}:{r['units'][v]}" for v in vs if v in r["units"])
        docs.append({"q": q, "rec": f"{r['f']} | {umap} | {r['cond']}", "lhs": lhs,
                     "name": r["name"], "head": r["f"],
                     "close": rng.choice(CLOSE), "why": rng.choice(WHY).format(f=r["f"])})
        calls.append(f"<tool>eval<arg>{expr}</tool>")
    out = re.findall(r"<res>(.*?)</res>",
          subprocess.run(["tools/eval/evalcli","-"], input="\n".join(calls)+"\n",
                         capture_output=True, text=True).stdout, re.S)
    built, dropped = [], 0
    for d, c, res in zip(docs, calls, out):
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
    print(f"    distinct 4-gram ratio        {D['distinct_4gram_ratio']:.4f}")
    print(f"    head coverage                {D['head_coverage']*100:.1f}%  ({D['heads_used']} heads)")
    print(f"    mean phrasing entropy        {D['mean_phrasing_entropy_bits']:.2f} bits/head")
    SP = diversity_by_span(docs)
    print(f"    per-span 4-gram ratio        question {SP['question']:.4f}   "
          f"record {SP['record']:.4f}   answer {SP['answer']:.4f}")
    print(f"      (the composite above is dragged down by the record span, which is a canonical")
    print(f"       retrieval record and is SUPPOSED to repeat -- bounded by head count alone)")
    D.update({"span_"+k: v for k, v in SP.items()})
    pathlib.Path("corpus/synth_sample.jsonl").write_text(
        "\n".join(json.dumps(d) for d in docs))
    json.dump(D, open("corpus/diversity.json","w"), indent=1)
    print("\n  sample:"); [print("   ", d["text"][:150]) for d in docs[:3]]
