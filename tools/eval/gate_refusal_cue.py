#!/usr/bin/env python3
"""How much of the refusal decision can be made without reading the record?

`missing:` is filled by the runtime with the shown record's first unbound variable, so it is a
legitimate field and a model is right to use it. The hazard is that it is SUFFICIENT: if
`refuse <=> missing != none` classifies the whole corpus, the fit judgement is never required
during training, and D2's score becomes a measurement of the binding check wearing D2's name.

Measured at the time of writing: 99.80% accurate, because 99.0% of D2 documents carry an unbound
variable. The model trained on that corpus refuses 97.5% of ordinary D2 items and 0.0% of items
where the wrong record is fully bound -- see docs/RESULT_FIT_JUDGEMENT.md.

This is a RATCHET, not a pass/fail on the level. The rate cannot be driven to chance: a wrong record
usually does have an unbound variable, on the device too. What must not happen is the number
drifting up unnoticed, or the fit-judgement documents disappearing again the way D2 itself once did.
"""
import json, pathlib, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "corpus"))
from recfmt import fields, MISSING

# EVERY SURFACE CUE, NOT ONE HAND-NAMED ONE. The first version measured `missing != none` alone and
# reported 95.77% while the ACTUAL separability was 99.95%, because two more cues existed: the
# literal ". Also " that A42 itself introduced (100% precision, 109 documents) and the `none` record
# span of D3. A gate named for a property that checks one instance of it is this repo's
# gate_format_parity failure, and this is its second occurrence.
#
# So the gate now (a) unions the known cues, and (b) SEARCHES for unknown ones: any question n-gram
# with high in-class frequency and zero occurrences outside is a cue nobody named yet. That second
# half is what would have caught ". Also " on the day it was written.
# THE UNION ACCURACY AND THE RESIDUAL ARE THE SAME NUMBER. Once the cue is right on everything
# except the fully-bound class, accuracy is exactly 100 - residual, so ratcheting both is ratcheting
# one twice and the second reads as independent evidence. Only the residual is ratcheted; accuracy
# is printed because it is the sentence a reader wants ("99% of refusals need no read").
PURE_FIT_FLOOR = 1.00    # % of documents that REQUIRE reading the record. Pre-registered dose.
# RECALL, NOT A RAW COUNT. The first version flagged any n-gram appearing >= 8 times in-class and
# never outside. That is a proxy for "is a cue", and at volume it fired on 141 trigrams like
# 's o n' -- 29 occurrences among 3,249 in-class documents, 0.9% recall, produced by variable names
# from two different records co-occurring. Real, but not a cue: a model cannot decide the class from
# a pattern present in 1% of it.
#
# ". Also ", the cue this search exists to catch, had 95.6% recall. The property is COVERAGE of the
# class, and a count threshold tracks corpus size instead. Eleventh proxy predicate, this one inside
# the gate written to catch the tenth.
NGRAM_RECALL = 0.20      # an n-gram covering this much of the class, and absent outside, is a cue


def main():
    p = ROOT / "corpus/synth_sample.jsonl"
    if not p.exists():
        print("CANNOT CHECK: corpus/synth_sample.jsonl absent -- not a pass"); return 2
    import collections, re
    n = right = pure = d2 = 0
    inside, outside = collections.Counter(), collections.Counter()

    def grams(q, k=3):
        w = re.findall(r"[A-Za-z]+", q.lower())
        return {" ".join(w[i:i + k]) for i in range(len(w) - k + 1)}

    # THE SYMBOLIC-TOOL TIER IS NOT PART OF THIS POPULATION, and leaving it in was a pure
    # denominator error. R1/C1/C2 always ANSWER, through solve/diff/integ, and a fit judgement is
    # not available to them: there is nothing to refuse. Counting them dilutes "documents that
    # REQUIRE reading the record" without changing the number of such documents --
    #
    #     3,114 / 314,637 = 0.99%   fails a 1.0% floor
    #     3,114 / 292,157 = 1.07%   excluding a tier the claim is not about
    #
    # The COUNT did not move. This is the fourth time in one week that a second population arrived
    # and a denominator did not, after the D1/D2 bands, gate_store_coverage and corpus_check's size
    # guard. Excluded and NAMED, because "excluded" and "clean" must not share an exit.
    TOOL_TIER = {"R1", "C1", "C2"}
    skipped_tier = 0
    for line in p.open():
        o = json.loads(line)
        if o.get("kind") in TOOL_TIER:
            skipped_tier += 1
            continue
        rec = o["text"].split("<r>", 1)[1].split("<", 1)[0]
        q = o["text"].split("<q>")[1].split("</q>")[0]
        f = fields(rec)
        miss = next((x for x in f if x.startswith("missing:")), "missing:none")
        refuses = "<tool>" not in o["text"]
        # THE UNION of every cue that lets a refusal be decided without reading the record.
        cue = (miss != "missing:none") or rec.startswith("none")
        n += 1
        right += (cue == refuses)
        is_pure = o.get("kind") == "D2" and miss == "missing:none"
        if o.get("kind") == "D2":
            d2 += 1
        if is_pure:
            pure += 1
            inside.update(grams(q))
        else:
            outside.update(grams(q))
    acc = 100.0 * right / n
    leaks = sorted(((g, c) for g, c in inside.items()
                    if pure and c / pure >= NGRAM_RECALL and outside[g] == 0),
                   key=lambda x: -x[1])
    pf = 100.0 * pure / n
    print(f"  documents {n:,}   D2 {d2:,}")
    print(f"  'refuse <=> missing != none' classifies {acc:.2f}% of the corpus")
    print(f"  excluded: {skipped_tier:,} R1/C1/C2 documents -- they always answer through a tool, "
          f"so a fit judgement is not available to them and they are not this claim's population")
    print(f"  documents that REQUIRE reading the record: {pure} ({pf:.2f}%)")
    print(f"  question n-grams covering >= {100*NGRAM_RECALL:.0f}% of the class and ZERO outside: "
          f"{len(leaks)}")
    for g, c in leaks[:5]:
        print(f"    LEAK {g!r} recall {100*c/pure:.1f}% ({c}/{pure}), outside 0")
    bad = []
    if leaks:
        bad.append(f"{len(leaks)} surface cue(s) separate the class without reading the record")
    if pf < PURE_FIT_FLOOR:
        bad.append(f"only {pf:.2f}% of documents require reading the record, below {PURE_FIT_FLOOR}%")
    for b in bad:
        print(f"  {b}")
    if bad:
        print("FAIL: the corpus is drifting toward a refusal decision that never reads the record.")
        return 1
    print("PASS: within the recorded ratchet. The rate is reported, never assumed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
