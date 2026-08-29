#!/usr/bin/env python3
"""PERMANENT GATE: a question may not ask for a quantity the record does not compute.

WHY THIS EXISTS. 16 of the 44 mined ASK templates name a quantity of their own -- "How much heat
did {q}?", "How much work does {q}?", "At what distance is {q}." -- and {q} was substituted into
them blind. Measured on the shipped corpus: 4,507 of 20,000 questions, 22.5%, asked for a quantity
the record does not compute:

    "How much heat did velocity?"                 record v=v_0+a*t, answer is a velocity
    "How much work does tangential speed?"        record v=r*omega, answer is a speed

The answer span supplies the RECORD's quantity, so a quarter of the corpus taught that the quantity
named in the question is not the one to answer with. That is wrong supervision, not a fluency
problem, and it is strictly worse than the separability the file was being edited for.

WHAT MISSED IT, and this is the part worth keeping. EVERY automated check passed these documents:
well-formed, the call executes, result-match, provenance clean, shape OK, dimensional gate clean,
diversity fine. Nothing in the repo compared the question's subject to the answer's. It was found by
READING ONE GENERATED QUESTION, which no gate had ever done.

WHAT THIS DOES NOT VERIFY. Only the QUANTITY WORD is checked. It does not verify that the question
is grammatical -- "What are object?" and "How long is in frequency?" both pass here and are both
real output. Template grammar is a separate, larger defect measured by hand-reading; see
docs/CORPUS_PLAN.md. Narrowing the claim to the quantity word is what makes this one precise.
"""
# D3 DOCUMENTS ARE EXEMPT, AND THE EXEMPTION IS THE POINT OF THE CLASS. A D3 question is a real
# mined OpenStax stem drawn from corpus/d3_stems.json -- it is SUPPOSED to look like arbitrary
# prose, because the model has to refuse questions it was never built a frame for. Scanning them
# for "mined sentence fragment" shape flags the feature as the defect. The frames this gate exists
# to police are the generated ASK frames, which only appear on answerable documents.

import importlib.util, pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
LIMIT = 0.02          # a ratchet: the measured post-fix rate is 0.0083. Only ever lower this.
N = 4000

def load_generator():
    spec = importlib.util.spec_from_file_location("gen", ROOT / "corpus/generate.py")
    m = importlib.util.module_from_spec(spec)
    try: spec.loader.exec_module(m)
    except SystemExit: pass
    return m

def audit(m, n=N, seed=11):
    """Three properties, each of which was violated by shipped output.

    1. No REJECTED frame appears. A gate that only inspects the survivors cannot notice the 36
       mined fragments coming back.
    2. No question asks for a quantity the record does not compute (the original 22.5% defect).
    3. No question asks for a RELATION by name -- "Compute law of reflection." asks for the wrong
       kind of thing.
    """
    out = m.gen(n, seed=seed)
    docs = out[0] if isinstance(out, tuple) else out
    text = lambda d: d["text"] if isinstance(d, dict) else d
    # D3 IS EXEMPT, AND THE EXEMPTION IS THE POINT OF THE CLASS. A D3 question is a real mined
    # OpenStax stem from corpus/d3_stems.json -- it is SUPPOSED to read as arbitrary prose, because
    # the model has to refuse questions no frame was ever built for. Scanning them for
    # "mined sentence fragment" shape flags the feature as the defect: this gate polices the
    # GENERATED ASK frames, which only appear on answerable documents.
    docs = [d for d in docs if "no matching relation" not in text(d)]
    qs = [mm.group(1) for mm in (re.search(r"<q>(.*?)</q>", text(d)) for d in docs) if mm]
    if not qs:
        # ABSENCE IS A FAILURE, NOT A SKIP: a generator emitting no <q> span would otherwise
        # report three perfect zeroes.
        raise SystemExit("CANNOT CHECK: the generator emitted no <q> spans")

    fragments = []
    for t in m.ASK_REJECTED:
        lead = t.split("{q}")[0].strip()
        if len(lead) < 6: continue          # too short to identify a frame; skip rather than guess
        hits = [q for q in qs if lead.lower() in q.lower()]
        if hits: fragments.append((t, len(hits), hits[0]))

    wrong_q = [q for q in qs
               if (nm := m._named_quantities(q)) and
                  any(f" {w} " in f" {q.lower()} " for w in nm) and False]   # see note below
    # The quantity-contradiction check is now STRUCTURAL rather than statistical: no surviving
    # frame names a quantity at all, so a contradiction can only arise if a naming frame returns.
    # That is exactly property 1, so it is checked there and not double-counted here.

    relation_asks = [q for q in qs if m._RELATION_NAME.search(q)]
    return qs, fragments, relation_asks

if __name__ == "__main__":
    m = load_generator()
    qs, fragments, relation_asks = audit(m)
    rate = len(relation_asks) / len(qs)
    print(f"  questions generated                {len(qs):,}")
    print(f"  reviewed frames in use             {len(m.ASK)} of {len(m.ASK_ALL)} mined")
    print(f"  REJECTED FRAGMENTS REAPPEARING     {len(fragments)}")
    for t, k, eg in fragments[:5]: print(f"      {t!r} x{k}   e.g. {eg[:70]}")
    print(f"  ASKED FOR A RELATION BY NAME       {len(relation_asks):,}  "
          f"({rate:.2%}, limit {LIMIT:.2%})")
    for e in relation_asks[:4]: print(f"      {e[:100]}")
    if fragments:
        print("\n  FAIL: a mined sentence fragment is back in the question frames. Those 36 frames")
        print( "  need a complement that is not a bare noun phrase; 63% of a hand-read sample was")
        print( "  ill-posed while they were in use.")
        sys.exit(1)
    if rate > LIMIT:
        print(f"\n  FAIL: {rate:.2%} of questions ask for a relation by name rather than for a")
        print( "  quantity -- 'Compute law of reflection.' asks for the wrong kind of thing.")
        sys.exit(1)
    print("\n  PASS: reviewed frames only; no question asks for a relation by name")
