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

KNOWLEDGE_KINDS = {"K1", "K2", "K3"}
BANDS = {"D1": (0.08, 0.12), "D2": (0.03, 0.07)}


def check(path="corpus/synth_sample.jsonl", verbose=True):
    """Assert the corpus composition. Returns (docs, counts) so a caller need not re-read the file."""
    docs = [json.loads(l) for l in open(path)]
    kinds = collections.Counter(d.get("kind", "answer") for d in docs)
    tot = sum(kinds.values())
    assert tot, f"{path} is empty -- an empty corpus trivially satisfies every band"

    compute = [d for d in docs if d.get("kind") not in KNOWLEDGE_KINDS]
    ctot = len(compute)
    assert ctot, "the corpus contains no compute documents at all"
    for kind, (lo, hi) in BANDS.items():
        share = sum(1 for d in compute if d.get("kind") == kind) / ctot
        assert lo <= share <= hi, (
            f"corpus: {kind} is {share:.1%} OF THE COMPUTE TIER ({ctot:,} documents), "
            f"expected {lo:.0%}-{hi:.0%}. The band is a share of the compute tier, not of the "
            f"whole file; see train/corpus_check.py.")

    assert any("fit:low" in d["text"] for d in docs), "corpus contains no fit:low documents"

    if verbose:
        nk = tot - ctot
        print("  corpus composition: "
              + "  ".join(f"{a} {100*b/tot:.0f}%" for a, b in kinds.most_common()), flush=True)
        if nk:
            kk = collections.Counter(d.get("kind") for d in docs if d.get("kind") in KNOWLEDGE_KINDS)
            print(f"  knowledge tier: {nk:,} documents ({100*nk/tot:.1f}% of the corpus) {dict(kk)}",
                  flush=True)
            print(f"  bands checked against the {ctot:,} compute documents, not the {tot:,} total",
                  flush=True)
    return docs, kinds


if __name__ == "__main__":
    import sys
    try:
        check()
        print("PASS corpus_check")
    except AssertionError as e:
        print(f"FAIL corpus_check: {e}")
        sys.exit(1)
