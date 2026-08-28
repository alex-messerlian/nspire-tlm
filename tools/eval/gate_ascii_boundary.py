#!/usr/bin/env python3
"""PERMANENT GATE: no character reaches the model that its tokenizer cannot represent.

THE FIRST VERSION OF THIS GATE ASKED "is it non-ASCII", AND THAT IS A PROXY. It fired on 5,202 of
29,985 documents (17.3%) because the refusal answers contain an EM DASH -- which is a SINGLE TRAINED
TOKEN in this vocabulary (id 433), present because FineWeb and OpenStax are full of them. Replacing
it with "--" would have cost MORE tokens (9 against 8) and made the corpus worse. The predicate would
have forced a pointless rewrite of a sixth of the corpus while calling it a fix.

The property is not "is this ASCII". It is "does the tokenizer have a token for this".

WHY, and it is measured rather than argued. The tokenizer is vocab-4096 trained on an ASCII corpus:

    'theta' -> ['Ġtheta']            ONE token, seen in an identifier slot
    'θ'     -> ['ĠÎ', '¸']           TWO tokens, byte fallback, never seen in that slot
    '√'     -> ['Ġâ', 'Ī', '<unk>']  an actual UNKNOWN token

That is the <res> failure mechanism exactly -- a token shape in a slot the model has never seen
there -- and for the radical it is not even a soft failure. So the corpus is ASCII-only and the
input boundary is where every surface (typed, keypad ctrl map, symbol picker) normalises.

THE PART THAT IS NOT ABOUT BYTES. Normalising to ASCII is not sufficient: a Greek glyph must map to
its NAME, never to a visually similar Latin letter. rho/p, tau/t, omega/w and sigma/s are ALL live
symbol pairs in store_clean.json, so 'rho' -> 'p' silently turns a density into a pressure and
leaves a well-formed document that every other gate passes. See docs/DESIGN_SYMBOLIC_INPUT.md §2.

WHEN THE UI LANDS, extend this gate to assert the picker's output is normalised BEFORE the prompt is
assembled, not merely before evaluation. Putting the requirement here means the next person extending
it trips on it instead of reading about it.
"""
import json, pathlib, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
CORPUS = ROOT / "corpus/synth_sample.jsonl"
LOOKALIKE = {"rho": "p", "tau": "t", "omega": "w", "sigma": "s", "nu": "v", "mu": "m"}


def main():
    if not CORPUS.exists():
        print(f"  CANNOT CHECK: {CORPUS} is missing. Refusing to report a pass.")
        return 2
    try:
        from tokenizers import Tokenizer
        tk = Tokenizer.from_file(str(ROOT / "train/tok4096.json"))
    except Exception as e:
        print(f"  CANNOT CHECK: tokenizer unavailable ({e}). Refusing to report a pass.")
        return 2

    # Classify each distinct non-ASCII character ONCE, by what the tokenizer does with it.
    verdict = {}
    def ok(ch):
        if ch in verdict: return verdict[ch]
        ids = tk.encode(ch).ids
        # unrepresentable: an <unk>, or byte-fallback into more than one piece
        verdict[ch] = not (0 in ids or len(ids) > 1)
        return verdict[ch]

    bad, n = [], 0
    with CORPUS.open() as f:
        for i, line in enumerate(f):
            try:    t = json.loads(line)["text"]
            except Exception:  continue
            n += 1
            offenders = sorted({c for c in t if ord(c) > 127 and not ok(c)})
            if offenders:
                bad.append((i, offenders[:6], t[:60]))
    print(f"  documents scanned: {n:,}")
    seen = sorted(verdict)
    if seen:
        good = [c for c in seen if verdict[c]]
        print(f"  distinct non-ASCII characters: {len(seen)}   "
              f"representable as ONE trained token: {len(good)} "
              f"({' '.join(f'U+{ord(c):04X}' for c in good[:6])})")
    if bad:
        for i, chars, snip in bad[:8]:
            cps = " ".join(f"U+{ord(c):04X}" for c in chars)
            print(f"  NON-ASCII  line {i}: {cps}\n             {snip}")
        print(f"\n  FAIL: {len(bad)} document(s) carry a codepoint the tokenizer cannot represent")
        print("  as a single token: an <unk>, or byte fallback into pieces never seen in that slot.")
        return 1
    if n == 0:
        print("  CANNOT CHECK: the corpus produced no documents. Not a pass.")
        return 2
    print("  PASS: every character in every document is a single trained token")
    print(f"  (reminder: normalise Greek to its NAME. {', '.join(f'{k}!={v}' for k, v in LOOKALIKE.items())}"
          " -- a lookalike map is a silent semantic substitution no other gate can see.)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
