#!/usr/bin/env python3
"""The written F1 explanations must be sayable by the device and true to their record.

corpus/knowledge/explanations.json carries three written variants for each of the 164 compute
records. They replace a template that produced "Newton's second law is F_net = m*a, with F_net in N,
m in kg and a in m/s^2" -- the answer the device test came back calling cheap.

Prose cannot be checked for being a good explanation. Everything here is a property that CAN be
checked, and each one is a defect that was actually found in this set:

  length      the device caps an answer at 90 tokens (device_app.c:524), ~230 chars at the
              measured 2.564 chars/token
  ASCII       the tokenizer has 4096 entries trained on this corpus; anything outside becomes <unk>
              and the answer is visibly broken on screen
  em dash     the user asked for none
  symbols     a variable named in an explanation must be one the RECORD declares, or the model is
              taught to invent one. gate_name_provenance polices record NAMES; this polices symbols
  glue        normalising "E=(Delta_m)(c)^(2)" to readable form dropped the implicit multiplication
              and produced "E = Delta_mc^2", which reads as ONE symbol. 7 variants carried it
  variety     three identical variants would make the dose meaningless
  coverage    a silently-empty file would restore the template with nothing saying so

THE SYMBOL CHECK NEEDS A MULTI-UNDERSCORE-AWARE PATTERN. The first version used
`[A-Za-z][A-Za-z0-9]*_[A-Za-z0-9]+`, which matches "Delta_E" inside "Delta_E_LK" and reported 20
undeclared symbols that were all the same false positive. Check the oracle before the subject.
"""
import json, pathlib, re, statistics, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
EXPL = ROOT / "corpus/knowledge/explanations.json"
HARD_CAP = 215

SYM = re.compile(r"(?<![A-Za-z0-9_])([A-Za-z][A-Za-z0-9]*(?:_[A-Za-z0-9]+)+)")
WORDS = {"Delta", "delta"}          # a bare Greek word is prose, not a symbol reference

F = 0


def ck(c, m, d=""):
    global F
    if not c:
        F += 1
    print(f"  {'PASS' if c else 'FAIL'}  {m}" + (f"\n        {d}" if d and not c else ""))


def main():
    if not EXPL.exists():
        print("CANNOT CHECK: corpus/knowledge/explanations.json absent. Not a pass.")
        return 2
    R = json.load(open(EXPL))
    store = {r["f"]: r for r in json.load(open(ROOT / "corpus/store_clean.json"))}
    V = [(r, v) for r in R for v in r["variants"]]

    ck(len(R) == len(store),
       f"every store record has written prose ({len(R)} of {len(store)})",
       f"missing: {sorted(set(store) - {r['formula'] for r in R})[:4]}")
    ck(all(len(r["variants"]) == 3 for r in R),
       "every record has exactly three variants",
       str([r["name"] for r in R if len(r["variants"]) != 3][:3]))
    ck(all(len(set(r["variants"])) == 3 for r in R),
       "and all three are distinct",
       str([r["name"] for r in R if len(set(r["variants"])) != 3][:3]))

    L = [len(v) for _, v in V]
    over = [(r["name"], len(v)) for r, v in V if len(v) > HARD_CAP]
    ck(not over, f"no variant exceeds {HARD_CAP} chars (median {statistics.median(L):.0f}, "
                 f"max {max(L)})", str(over[:3]))

    na = [(r["name"], v) for r, v in V if any(ord(c) > 127 for c in v)]
    ck(not na, f"every variant is pure ASCII ({len(na)} violations)", str(na[:2]))
    em = [(r["name"], v) for r, v in V if "—" in v or "–" in v or " -- " in v]
    ck(not em, f"no em dash, en dash or ' -- ' ({len(em)})", str(em[:2]))

    # SYMBOLS: a subscripted symbol must be declared by the record it explains.
    undeclared = []
    for r, v in V:
        rec = store.get(r["formula"])
        if not rec:
            continue
        allowed = set((rec.get("units") or {}).keys()) | {"pi", "e"}
        for tok in SYM.findall(v):
            if tok not in allowed and tok not in WORDS:
                undeclared.append((r["formula"], tok, v[:60]))
    ck(not undeclared, f"every subscripted symbol is declared by its record ({len(undeclared)})",
       str(undeclared[:3]))

    # GLUE: two declared variables run together read as one symbol the record does not have.
    #
    # MINIMUM LENGTH 2, NOT 3, and that gap let a control survive. The first version only looked at
    # tokens of three characters or more, so "kx" -- k and x glued, both single letters, and single
    # letters are most of this store's variables -- was invisible. mg, vt, IR and qE are all the
    # same shape.
    #
    # A 2-char token is also an English word ("at", "in", "is") whenever the record happens to
    # declare those letters, so the check is SCOPED TO A MATH CONTEXT rather than filtered by a
    # wordlist: the token must sit within a span that also carries an operator. That is structural,
    # and a wordlist would be the proxy predicate this repo has paid for nine times.
    glued = []
    MATHY = re.compile(r"[=*/^]")
    for r, v in V:
        rec = store.get(r["formula"])
        if not rec:
            continue
        vs = set((rec.get("units") or {}).keys())
        for m_ in re.finditer(r"(?<![A-Za-z0-9_])([A-Za-z][A-Za-z0-9_]+)(?![A-Za-z0-9_])", v):
            tok = m_.group(1)
            if tok in vs:
                continue
            lo, hi = max(0, m_.start() - 24), min(len(v), m_.end() + 24)
            if len(tok) < 3 and not MATHY.search(v[lo:hi]):
                continue                      # prose, not an expression
            for k in range(1, len(tok)):
                if tok[:k] in vs and tok[k:] in vs:
                    glued.append((r["formula"], tok, v[:60]))
                    break
    ck(not glued, f"no two declared variables are run together ({len(glued)})", str(glued[:3]))

    # The relation should actually appear: an explanation of a formula that never states it is a
    # different kind of answer, and the brief asks for "maybe with the formula".
    states = sum(1 for r, v in V if r["formula"].split("=", 1)[0].strip() in v)
    ck(states > 0.9 * len(V),
       f"{states} of {len(V)} variants name the quantity the record solves for "
       f"({100*states/len(V):.1f}%)")

    grounded = sum(1 for r in R if r.get("grounded"))
    print(f"        grounded in the record's own OpenStax section: {grounded} of {len(R)} "
          f"({100*grounded/len(R):.0f}%); the rest are written from the physics and flagged")

    # REPORTED, NOT GATED: an answer stating an applicability condition the PROMPT will not carry.
    # assemble.c writes `req ? req : "standard conditions"`, and only 4 of 164 records have a req,
    # so the model must recall these. That is memorisation, which is accepted here, but it is the
    # condition-field skew running in the other direction and it is not allowed to be invisible.
    cond = re.compile(r"\b(only (if|while|when|holds|true)|assumes?|breaks? down|stops holding|"
                      r"quits|throw this out|no longer|as long as|so long as|ignores?|neglect\w*|"
                      r"frictionless|weightless|near the axis|close to the axis)\b", re.I)
    skew = [(r["name"], v) for r, v in V
            if cond.search(v) and not (store.get(r["formula"], {}).get("req") or "").strip()]
    print(f"        OPEN, reported not gated: {len(skew)} variants state a condition their record's "
          f"`req` does not carry, so the prompt will read 'standard conditions'.")
    print(f"        docs/RESULT_F1_PROSE.md; either those conditions belong in req (and in "
          f"units_train.json and build/store.tns with it) or the answers cannot state them.")

    print(f"\n{'FAIL' if F else 'PASS'} gate_explanations: {len(R)} records, {len(V)} variants, "
          f"{F} failure{'' if F == 1 else 's'}")
    return 1 if F else 0


if __name__ == "__main__":
    sys.exit(main())
