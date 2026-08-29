#!/usr/bin/env python3
"""Build the certified out-of-scope stem pool that legitimate D3 documents are drawn from.

WHY THIS EXISTS. Removing generated D3 was correct -- 843 of 843 declined a question the store
answers -- but it left a hole: src/store/assemble.c:109 emits
`none | missing:none | no matching relation | fit:low` whenever the picker finds nothing, so the
RUNTIME can produce a shape the corpus no longer contains. A token shape in a slot the model has
never seen is the <res> failure, and it would have shipped.

The fix is not to put the old documents back. It is to draw D3 questions from OUTSIDE the store and
certify each one with the SAME criterion that condemned the old ones: a D3 is legitimate iff no
store record computes the quantity it asks for from the givens it supplies.

Eval stems are excluded, so the training pool cannot contaminate clean_surface.json.
"""
import json, pathlib, re, sys, importlib.util as u

ROOT = pathlib.Path(__file__).resolve().parents[2]
spec = u.spec_from_file_location("d3", ROOT / "tools/eval/d3_criterion.py")
d3 = u.module_from_spec(spec); spec.loader.exec_module(d3)


def main():
    recs = d3.load_store()
    stems = json.load(open(ROOT / "corpus/stems_all.json"))
    evalset = {x["stem"] for x in json.load(open(ROOT / "corpus/clean_surface.json"))["items"]}
    # HAND-READING 12 CERTIFIED STEMS FOUND 4 BAD, and none of them are visible to the criterion,
    # which is a mechanical store lookup over the stem AS GIVEN:
    #   * a SPANISH stem -- OpenStax's Spanish edition is in the mined pool;
    #   * stems TRUNCATED mid-sentence ("...to reach a temperature of -20.");
    #   * and the dangerous pair: truncated stems about topics the store CAN answer (momentum, an
    #     ice skater at 8 m/s; heat removal, Q=m*c*dT). They certify as out-of-scope only BECAUSE
    #     the truncation removed the givens, so the corpus would teach refusal on answerable topics.
    # A certified-legitimate stem still has to be a well-formed English question.
    NONASCII = re.compile(r"[^\x00-\x7f]")
    ASKS = re.compile(r"\?|^(what|which|how|why|find|calculate|determine|compute|show|prove|"
                      r"explain|describe|state|suppose|consider|evaluate|estimate|sketch|graph)\b",
                      re.I)
    TRUNC = re.compile(r"(\b(of|to|at|in|for|the|a|an|and|or|with|from|is|are|by)\s*[.]?\s*$)|"
                       r"([-+]?\d+[.]?\s*$)", re.I)
    seen, out, rejected = set(), [], 0
    dropped = {"nonascii": 0, "not_a_question": 0, "truncated": 0}
    for s in stems:
        s = (s or "").strip()
        if len(s) < 25 or len(s) > 220:
            continue
        # STRIP LEAKED EXERCISE NUMBERING. "(a) How much heat transfer occurs..." -- the project log
        # already records this class from the ASK-frame mining ("five with (a) leaked from exercise
        # numbering"). It was in 3 of 12 hand-read stems.
        s = re.sub(r"^\(?[a-z]\)\s*", "", s).strip()
        s = s[:1].upper() + s[1:] if s else s
        if len(s) < 25 or s in evalset or s in seen:
            continue
        seen.add(s)
        if NONASCII.search(s):            dropped["nonascii"] += 1;        continue
        if not ASKS.search(s.strip()):    dropped["not_a_question"] += 1;  continue
        if TRUNC.search(s.strip()):       dropped["truncated"] += 1;       continue
        if d3.answerable_by(recs, set(d3.GIVEN.findall(s)), s.lower()):
            rejected += 1
            continue
        out.append(s)
    print(f"  stems considered : {len(seen):,}")
    print(f"  rejected (a store record answers them) : {rejected:,}")
    for k, v in dropped.items(): print(f"  dropped, {k:16}: {v:,}")
    print(f"  CERTIFIED out-of-scope : {len(out):,}")
    json.dump({"_doc": ("Stems certified out-of-scope by tools/eval/d3_criterion.py. Eval stems "
                        "excluded so this cannot contaminate clean_surface.json. Rebuild with "
                        "tools/eval/build_d3_pool.py whenever the store changes."),
               "stems": out}, open(ROOT / "corpus/d3_stems.json", "w"), indent=0)
    return 0 if out else 2


if __name__ == "__main__":
    sys.exit(main())
