#!/usr/bin/env python3
"""THE D3 CRITERION, stated before it is applied, and decidable by lookup rather than judgment.

    A D3 document ("none | missing:none | no matching relation | fit:low") is LEGITIMATE iff the
    store contains NO record that computes the quantity the question asks for from the givens the
    question supplies, plus any constant the store itself resolves.

    It is ILLEGITIMATE -- a refusal rolled onto an answerable question -- iff some record does.

WHY A CRITERION AND NOT AN ADJUDICATION. The n=600 read confirmed 4 of 13 D3 documents as defects
and rejected 9 of the IDENTICAL mechanism, with directly contradictory reasoning and no stated rule.
That split is worth ~2.2 pp of the headline rate -- larger than any real effect the read could
detect -- so it is an instrument defect, not a measurement. A criterion that a reader applies by
judgment reproduces the problem; this one is a store search.

WHAT IT DELIBERATELY DOES NOT ASK. Whether refusing is "faithful to the prompt the model sees". It
is -- the prompt says `none`, so declining is locally correct. That reading makes every D3 document
correct by construction and the class unfalsifiable. The question that matters for SUPERVISION is
whether the runtime could ever put the model in that state for this question, and generate.py builds
the question FROM a real record before overwriting the record span, so it cannot.
"""
import json, pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
VAR = re.compile(r"(?<![A-Za-z0-9_])([A-Za-z][A-Za-z0-9_]*)(?![A-Za-z0-9_(])")
GIVEN = re.compile(r"([A-Za-z][A-Za-z0-9_]*)\s*=\s*-?[\d.]+(?:[eE][-+]?\d+)?")
RES = {"pi", "e", "sin", "cos", "tan", "ln", "log", "sqrt", "exp"}
_STOP = {"from", "with", "that", "than", "into", "over", "under", "using", "given", "when",
         "where", "which", "this", "these", "some", "each", "both", "upward", "positive"}


def load_store():
    d = json.loads((ROOT / "corpus/store_clean.json").read_text())
    return d if isinstance(d, list) else d.get("records", [])


def answerable_by(recs, givens, question_lc):
    """Records whose free RHS variables are all supplied, and whose LHS quantity the question asks
    for -- matched on the LHS symbol OR on words from the record's name."""
    hits = []
    for r in recs:
        f = r.get("f", "")
        if "=" not in f:
            continue
        lhs, rhs = f.split("=", 1)
        lhs = lhs.strip()
        free = {v for v in VAR.findall(rhs)} - RES
        cval = set((r.get("cval") or {}).keys())
        if not free:
            continue
        if not (free - cval) <= givens:
            continue
        if lhs in givens:                      # the question already supplies the answer
            continue
        name = (r.get("name") or "").lower()
        asks_symbol = re.search(rf"(?<![A-Za-z0-9_]){re.escape(lhs)}(?![A-Za-z0-9_])", question_lc) \
                      or re.search(rf"(?<![A-Za-z0-9_]){re.escape(lhs.lower())}(?![A-Za-z0-9_])", question_lc)
        # ANY head word, not the first TWO. Requiring two was a proxy for "the question asks for
        # this quantity" and it missed every record whose name carries a qualifier the question
        # omits: "velocity in free fall, upward positive" against a question saying "velocity",
        # "mass from energy" against "mass". Three of the thirteen disputed documents were scored
        # LEGITIMATE by that predicate and are answerable by inspection -- v=v_0-g*t, m=Delta_E/c^2
        # and f=c/lambda, each with the question supplying exactly that record's inputs.
        head = [w for w in re.findall(r"[a-z]{4,}", name) if w not in _STOP]
        asks_name = any(w in question_lc for w in head)
        # And the decisive structural signal, which needs no name at all: the question supplies
        # EXACTLY this record's free inputs and does not supply its output. The generator builds
        # the question from a record before overwriting the record span, so an exact match is a
        # fingerprint of the source record.
        exact_inputs = (free - cval) == givens
        if asks_symbol or asks_name or exact_inputs:
            hits.append((r.get("name", "?"), f))
    return hits


def main():
    recs = load_store()
    corpus = ROOT / "corpus/synth_sample.jsonl"
    if not corpus.exists():
        print("  CANNOT CHECK: corpus/synth_sample.jsonl is missing.")
        return 2
    total = illegit = 0
    examples = []
    for i, line in enumerate(corpus.open()):
        t = json.loads(line)["text"]
        if "no matching relation" not in t:
            continue
        total += 1
        q = re.search(r"<q>(.*?)</q>", t, re.S)
        if not q:
            continue
        q = q.group(1)
        givens = set(GIVEN.findall(q))
        hits = answerable_by(recs, givens, q.lower())
        if hits:
            illegit += 1
            if len(examples) < 6:
                examples.append((i, q[:74], hits[0]))
    print(f"  D3 documents in corpus: {total}")
    print(f"  ILLEGITIMATE by the criterion (a store record answers them): {illegit}"
          f"  = {100*illegit/max(1,total):.1f}%")
    print(f"  LEGITIMATE: {total-illegit} = {100*(total-illegit)/max(1,total):.1f}%")
    print()
    for i, q, (nm, f) in examples:
        print(f"  line {i}: {q}")
        print(f"      answered by: {nm}  ->  {f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
