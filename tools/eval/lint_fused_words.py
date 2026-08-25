"""PERMANENT LINT: words fused at a mining line-wrap, e.g. 'inthe', 'aninclined', 'thatdiffer'.

Mined OpenStax text loses the space at some line wraps. Under E the record NAME IS THE INTERFACE --
it is what a student picks by -- so a fused name is a user-facing defect, not a cosmetic one.

Detection is precise rather than heuristic: an unknown word that splits cleanly into a STOPWORD
plus a known word. That shape matched 4 of 4 real cases and admits almost nothing else. A general
"unknown word" check does NOT work -- the system dictionary lacks plurals and participles, so it
flags 'resistors', 'waves' and 'stored'. Tried first, retracted, replaced with this.

Does NOT verify: spelling of physics terms absent from the dictionary, transposed letters within a
word, or a fused pair where BOTH halves are non-stopwords ('forceof' is caught, 'energymass' is not).

A CLEAN LINT IS NOT A CLEAN BATCH. 'Magnetic field due to along straight wire' -- 'to a long' fused
into 'to along' -- passes here forever, because 'along' is a real word. This lint retires ONE SHAPE
of typo. It does not retire reading the names. Its precision comes from narrowing the claim to that
shape, not from a better dictionary: the general 'unknown word' version was tried and retracted.
"""
import json, re, sys, pathlib

STOP = {"the","a","an","of","in","for","to","and","is","are","with","at","on","by","that",
        "from","into","over","under","as","or","its","this","two","no"}

def load_words():
    p = pathlib.Path("/usr/share/dict/words")
    if not p.exists(): return None
    return {w.strip().lower() for w in p.open()}

def _known(w, words):
    """The 1934 Webster's lacks inflected forms: it has 'incline' but not 'inclined', which made
    'aninclined' a FALSE NEGATIVE on the first version. Strip common suffixes before giving up."""
    if w in words: return True
    for suf, repl in (("ed",""), ("ed","e"), ("s",""), ("es",""), ("ing",""), ("ing","e"),
                      ("d",""), ("ly",""), ("al","")):
        if w.endswith(suf):
            stem = w[:-len(suf)] + repl
            if len(stem) > 2 and stem in words: return True
    return False

def fused(name, words):
    out = []
    for w in re.findall(r"[a-z]+", name.lower()):
        if len(w) < 5 or _known(w, words): continue
        for k in range(1, len(w) - 2):
            if w[:k] in STOP and _known(w[k:], words):
                out.append((w, f"{w[:k]} {w[k:]}")); break
    return out

if __name__ == "__main__":
    words = load_words()
    if words is None:
        # EXIT 2, NOT 0. "cannot check" and "checked and clean" must not share an exit status:
        # a CI host without the wordlist would report this lint green forever.
        print("CANNOT CHECK: no /usr/share/dict/words on this host", file=sys.stderr)
        sys.exit(2)
    store = json.load(open(sys.argv[1] if len(sys.argv) > 1 else "corpus/store_clean.json"))
    bad = 0
    for r in store:
        for w, fix in fused(r["name"], words):
            print(f"FUSED {r['rid']}  {w!r} -> {fix!r}   in {r['name']!r}")
            bad += 1
    print(f"{bad} fused word(s) across {len(store)} records")
    sys.exit(1 if bad else 0)
