#!/usr/bin/env python3
"""The pre-run corpus composition check. ONE implementation, imported, never rewritten.

WHY IT EXISTS AT ALL. A training run once began against a corpus that had silently lost a refusal
class, and nothing said so until the arms came back wrong. The bands catch a class that LOST
documents, before twenty minutes of GPU are spent on it.

WHY IT IS A MODULE. The same nine lines lived in train/select_run.py and train/refusal_run.py, and
the knowledge tier broke BOTH in the same way on the same day. A defect in two implementations is a
missing abstraction, not two lapses -- the rule this repo wrote after generation loops, after
mutate-and-restore, and after two rankers that disagreed.

THE BANDS ARE SHARES OF THE COMPUTE TIER, NOT OF THE WHOLE FILE. They were set when the corpus held
only compute documents. The knowledge tier adds ~52,000 documents containing no D1 or D2 BY
CONSTRUCTION, which drags D1 from 9.5% to 7.8% and trips an 8% floor with nothing wrong. Widening
the band to admit that would blind it to the defect it exists for; dividing by the population the
band actually describes keeps it exactly as strict as it was.

Same shape as gate_store_coverage counting knowledge records against the compute denominator, and
as head coverage printing 979.9%: a second population arrived and the denominator did not move.
"""
import collections
import json
import os

KNOWLEDGE_KINDS = {"K1", "K2", "K3"}

# THE SYMBOLIC-TOOL TIERS, AND THE THIRD TIME THIS EXACT MISTAKE WOULD HAVE BEEN MADE.
#
# R1 (solve), C1 (diff) and C2 (integ) are sized by the STORE AND THE DOSE -- 338 rearrangement
# pairs at 30 each, 398 derivative pairs at 24 -- and not by generate.py's N, exactly like the
# knowledge tier. Counting them against a 240,000 target fails a correct corpus by precisely their
# size: 262,250 against 240,000, and the difference is 22,480, which is them.
#
# The docstring above already records this class twice (head coverage printing 979.9%,
# gate_store_coverage counting knowledge records against 164) and the file still carried one
# denominator. A second population arriving is not a rare event in this project; it is what every
# week of it has produced. So the partition is NAMED now rather than implied, and the next tier
# needs one line here instead of a debugging session before a training run.
TOOL_TIER_KINDS = {"R1", "C1", "C2"}
SIZED_ELSEWHERE = KNOWLEDGE_KINDS | TOOL_TIER_KINDS
BANDS = {"D1": (0.08, 0.12), "D2": (0.03, 0.07)}

# THE CORPUS SIZE IS PART OF THE EXPERIMENT AND IT IS THE COMPUTE TIER'S SIZE.
#
# corpus/generate.py's N argument sizes the COMPUTE corpus; the knowledge tier is a fixed 52,385
# documents determined by the glossary and the dose, not by N. Checking the total against 240,000
# therefore fails a correct corpus by exactly the size of the tier, which is the fourth time today
# that a second population arrived and a denominator did not move -- after head coverage printing
# 979.9%, gate_store_coverage counting knowledge records against 164, and the D1/D2 bands.
#
# The guard itself is not weakened. It exists because generate.py defaults to N=10,000 while run 2
# trained on 239,942, so an ordinary `python corpus/generate.py` between runs silently shrinks the
# compute corpus 24x and the run still converges and is comparable to nothing. That is still caught,
# against the population the number describes.
COMPUTE_DOCS = 240000
SIZE_TOL = 0.02


def check(path="corpus/synth_sample.jsonl", verbose=True):
    """Assert the corpus composition. Returns (docs, counts) so a caller need not re-read the file."""
    docs = [json.loads(l) for l in open(path)]
    kinds = collections.Counter(d.get("kind", "answer") for d in docs)
    tot = sum(kinds.values())
    assert tot, f"{path} is empty -- an empty corpus trivially satisfies every band"

    compute = [d for d in docs if d.get("kind") not in SIZED_ELSEWHERE]
    ctot = len(compute)
    assert ctot, "the corpus contains no compute documents at all"
    for kind, (lo, hi) in BANDS.items():
        share = sum(1 for d in compute if d.get("kind") == kind) / ctot
        assert lo <= share <= hi, (
            f"corpus: {kind} is {share:.1%} OF THE COMPUTE TIER ({ctot:,} documents), "
            f"expected {lo:.0%}-{hi:.0%}. The band is a share of the compute tier, not of the "
            f"whole file; see train/corpus_check.py.")

    assert any("fit:low" in d["text"] for d in docs), "corpus contains no fit:low documents"

    want = int(os.environ.get("CORPUS_DOCS", COMPUTE_DOCS))
    assert abs(ctot - want) <= SIZE_TOL * want, (
        f"the COMPUTE tier holds {ctot:,} documents, expected {want:,} "
        f"(+/-{100*SIZE_TOL:.0f}%). Regenerate with `python corpus/generate.py {want}`, or set "
        f"CORPUS_DOCS deliberately. A run at the wrong corpus size converges and is comparable to "
        f"nothing. The knowledge tier is sized by the glossary and the dose, not by N, so it is "
        f"counted separately.")

    if verbose:
        nk = tot - ctot
        print("  corpus composition: "
              + "  ".join(f"{a} {100*b/tot:.0f}%" for a, b in kinds.most_common()), flush=True)
        if nk:
            kk = collections.Counter(d.get("kind") for d in docs if d.get("kind") in KNOWLEDGE_KINDS)
            tk = collections.Counter(d.get("kind") for d in docs if d.get("kind") in TOOL_TIER_KINDS)
            nkk, ntk = sum(kk.values()), sum(tk.values())
            print(f"  knowledge tier:     {nkk:,} documents ({100*nkk/tot:.1f}% of the corpus) "
                  f"{dict(kk)}", flush=True)
            print(f"  symbolic-tool tier: {ntk:,} documents ({100*ntk/tot:.1f}%) {dict(tk)} "
                  f"-- sized by the store and the dose, not by N", flush=True)
            print(f"  bands and size checked against the {ctot:,} compute documents, "
                  f"not the {tot:,} total", flush=True)
    return docs, kinds


if __name__ == "__main__":
    import sys
    try:
        check()
        print("PASS corpus_check")
    except AssertionError as e:
        print(f"FAIL corpus_check: {e}")
        sys.exit(1)
