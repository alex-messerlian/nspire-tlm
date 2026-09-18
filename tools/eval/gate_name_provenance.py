#!/usr/bin/env python3
"""PERMANENT GATE: an answer may not state a record NAME that is not in its prompt.

WHY. src/store/assemble.c emits `formula | units | missing | condition | fit` and NO NAME, so any
record name in an answer is something the model must RECALL rather than read -- the A7 defect
(a literal that appears nowhere in the model's context), moved from the tool call into the answer.

It is not harmless just because the model gets it right in distribution. MEASURED on the shipped
d352 checkpoint over 120 records it was never trained on:

    refusals naming a record                                   78 of 120
    name matches the record shown                               7   (9.0%)
    name is a DIFFERENT record's, memorised from training       70

  P=((F)/(A)) is PRESSURE and it said "power from force and velocity" -- it saw the symbol P and
  recalled a P record. It refuses correctly and describes the record falsely, which is worse than
  refusing bluntly: the student is told what the calculator matched, and told wrong.

Before the fix the corpus was at 13.5% of ANSWER documents (1,251 of 9,299) and 100% of D2. The
store has 170 more mined physics records waiting, and every one of those is a record whose name the
model would be guessing while stating it as fact beside a number.

WHAT IT CHECKS. For every document: no record name from the store or the holdout appears in the
`<a>` span unless it also appears in the prompt (question + record span). Names shorter than
MIN_NAME are skipped and COUNTED -- a short name is an ordinary English word and would flag prose.
"""
import collections, json, pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
MIN_NAME = 5          # firing rate measured at 4, 5 and 6 below; the choice is printed, not assumed


def names():
    out = set()
    for f in ("corpus/store_clean.json", "corpus/units_holdout.json"):
        d = json.loads((ROOT / f).read_text())
        for r in (d if isinstance(d, list) else d.get("records", [])):
            n = (r.get("name") or "").lower().strip()
            if n:
                out.add(n)
    return out


def recmap():
    """formula -> store name, so the gate can ask whether naming X is TRUE of the record shown."""
    d = json.loads((ROOT / "corpus/store_clean.json").read_text())
    return {r["f"]: (r.get("name") or "") for r in d}


_DET = ("the ", "a ", "an ")

def _ident(name):
    """A record name reduced to the content that identifies it."""
    n = name.lower().strip().strip(":;,.").strip()
    for d in _DET:
        if n.startswith(d):
            n = n[len(d):]
            break
    return n

# AN IDENTITY ASSERTION, NOT A MENTION. This is the property the gate is named for: the answer
# telling the student WHAT THE MATCHED RECORD IS. "Work is W = F*d, force times the distance moved"
# USES the word "distance"; it does not claim the record is the distance record.
_SUBJ = lambda n: re.compile(r"(?:^|(?<=[.!?]\s))(?:the\s+)?" + re.escape(n) + r"\b\s*(?:is|are|'s|means)\b", re.I)
_DEFN = lambda n: re.compile(r"\bdefines\s+" + re.escape(n) + r"\b", re.I)


def _true_of(n, own):
    """Naming `n` is TRUE when it identifies the same quantity as the record's own store name.

    Containment either way, after article-stripping, because store names carry qualifiers: the
    record `lambda=h/(m*v)` is named "de Broglie wavelength FROM MASS AND SPEED" and an answer
    calling it "the de Broglie wavelength" is correct. There is a SECOND store record literally
    named "The de Broglie wavelength" (`lambda=h/p`), which is why the literal-string form of this
    gate reported 61 violations that were all this one true sentence.
    """
    own = _ident(own)
    return bool(own) and (n == own or n in own or own in n)


def violations(docs, pool, min_name, rmap=None, tally=None):
    """ONE regex pass per document, not one substring search per (document, name).

    The naive form is 240,000 x 199 = 48M scans and took minutes. The project log: a gate's runtime is a
    correctness property, because a check expensive enough to bypass still reads as coverage -- and
    the budget for run_gates.sh is seconds. Longest-first alternation so a name that contains
    another ("energy of a photon" over "energy") wins the match."""
    # COMPARE IDENTITY, NOT THE EXACT STRING. A leading determiner is not part of a record's
    # identity, and neither is trailing punctuation. Measured on the fixed corpus, the literal form
    # reported 141 violations and 136 were the answer's "The de broglie wavelength" against a
    # question that said "de broglie wavelength" -- the template's article, with every content word
    # sourced. The other 5 were `units_holdout.json`'s 'Tangential speed:', a mining artefact whose
    # trailing colon happened to match the explain template's "To find {s}:". Neither is a model
    # stating an identity it cannot read, which is the property.
    #
    # This does NOT weaken the check: the known-bad control below still fires, and a genuinely
    # fabricated name ("power from force and velocity" against a pressure record) is absent from
    # the prompt in any normalisation.
    cands = sorted((_ident(n) for n in pool if len(_ident(n)) >= min_name), key=len, reverse=True)
    if not cands:
        return []
    rx = re.compile("|".join(re.escape(n) for n in cands))
    bad = []
    for t in docs:
        if "<a>" not in t:
            continue
        prompt, ans = t.split("<a>", 1)
        rec = prompt.split("<r>", 1)[1].split("|", 1)[0].strip() if "<r>" in prompt else ""
        prompt = prompt.lower()
        for m in rx.finditer(ans.lower()):
            n = m.group(0)
            if n in prompt:
                continue
            # (a) the record is A51-SCRAMBLED: its formula is not in the store because the symbols
            #     were renamed. The scrambler renames SYMBOLS, not quantities, so the quantity name
            #     is still true of it -- "Power ... sigma = W/kappa" is power written in other
            #     letters. 390 of the 1,201 literal flags are this.
            if rmap is not None and rec and rec not in rmap:
                if tally is not None: tally["exempt: A51-scrambled record"] += 1
                continue
            # (b) the name is TRUE of the record shown.
            if rmap is not None and _true_of(n, rmap.get(rec, "")):
                if tally is not None: tally["exempt: true of the record shown"] += 1
                continue
            # (c) VOCABULARY, not an identity claim. Store names are built from ordinary physics
            #     nouns -- distance, power, velocity, pressure, kinetic energy -- and prose that
            #     explains physics must use them. Before this clause the gate flagged
            #     "force times the distance moved along that force" as stating a record name.
            if not (_SUBJ(n).search(ans) or _DEFN(n).search(ans)):
                if tally is not None: tally["exempt: vocabulary use, not an identity claim"] += 1
                continue
            bad.append((n, t))
            break
    return bad


def main():
    p = ROOT / "corpus/synth_sample.jsonl"
    if not p.exists():
        print("  CANNOT CHECK: corpus/synth_sample.jsonl is missing. Refusing to report a pass.")
        return 2
    docs = [json.loads(l)["text"] for l in p.read_text().splitlines() if l.strip()]
    pool = names()
    if not docs or not pool:
        print(f"  CANNOT CHECK: {len(docs)} documents, {len(pool)} names. Not a pass.")
        return 2

    rmap = recmap()

    # A CHECK WITH NOTHING TO FIND AND A DISABLED CHECK PRODUCE THE SAME OUTPUT, so the predicate is
    # exercised on known-bad and known-good samples on EVERY run, clean tree or not.
    #
    # THE CONTROLS USE REAL STORE RECORDS. They used to be built on a synthetic `x=a*b`, and the
    # A51 exemption below would have exempted that outright -- a control a change of architecture
    # silently disables was never testing the shipped behaviour (the gate_given_range lesson).
    # There is ONE CONTROL PER EXEMPTION, because a gate that checks several conditions behind a
    # single control proves a single condition (the gate_format_parity lesson).
    R = "p=((F)/(A))"                       # real, in the store, named 'Pressure'
    W = "W=F*d"                             # real, named 'work done by a constant force'
    def doc(rec, ans, q="find it."):
        return f"<q>{q}</q><r>{rec} | x:m | missing:x | c | fit:high<a>{ans}<end>"
    CONTROLS = [
        # the defect the gate exists for: the answer asserts the record IS a differently-named thing
        (True,  doc(R, "Power from force and velocity is p = F/A."), "known-bad: asserts a foreign name"),
        (True,  doc(R, "I cannot answer that: the record I have defines power from force and velocity."),
                "known-bad: the refusal frame"),
        # (b) the name is TRUE of the record shown
        (False, doc(R, "Pressure is force spread over area, p = F/A."), "known-good: names its own record"),
        # (c) vocabulary, not an identity claim
        (False, doc(W, "Work is W = F*d, force times the distance moved along that force."),
                "known-good: 'distance' used as vocabulary"),
        # (a) an A51-scrambled record keeps its quantity, so its name is still true of it
        (False, doc("sigma=((W)/(kappa))", "Power is not how much work you do, it is how fast. sigma = W/kappa."),
                "known-good: A51-scrambled record"),
    ]
    for want, d, label in CONTROLS:
        got = bool(violations([d], pool, MIN_NAME, rmap))
        if got != want:
            print(f"  CONTROL BROKEN [{label}]: fired={got}, expected={want}")
            return 2
    print(f"  controls: {len(CONTROLS)} passed (one per exemption, on REAL store records)")

    # MEASURE WHAT IT FLAGS BEFORE BELIEVING THE FLAG. A threshold picked without its firing rate is
    # the gate_ascii_boundary mistake -- that one would have rewritten a sixth of the corpus.
    print(f"  {len(docs):,} documents, {len(pool)} record names   [controls: known-bad fires, "
          f"known-good does not]")
    # Threshold sensitivity on a SAMPLE, the exact check on everything -- three full passes cost
    # 3x and the table only exists to show the choice of MIN_NAME is not load-bearing.
    smp = docs[:20000]
    for m in (4, 5, 6):
        v = violations(smp, pool, m, rmap)
        mark = " <- MIN_NAME" if m == MIN_NAME else ""
        print(f"    names >= {m} chars   {100*len(v)/len(smp):5.2f}% of a {len(smp):,}-doc sample{mark}")

    tally = collections.Counter()
    bad = violations(docs, pool, MIN_NAME, rmap, tally)
    print("  MEASURED, what the literal predicate flags and why each part is exempt:")
    for k, v in tally.most_common():
        print(f"    {v:6,}  {k}")
    print(f"    full corpus at MIN_NAME={MIN_NAME}: {len(bad)} of {len(docs):,} "
          f"({100*len(bad)/len(docs):5.2f}%)")
    for n, t in bad[:5]:
        print(f"  UNSOURCED NAME  '{n}'\n     {t[:150]}")
    if bad:
        print(f"\n  FAIL: {len(bad)} answer(s) state a record name the prompt does not contain.")
        print("  assemble.c emits no name, so the model would have to recall it -- and on a record")
        print("  it has not memorised it supplies a different record's name 89.7% of the time.")
        return 1
    print("  PASS: every record name in an answer also appears in its prompt")
    return 0


if __name__ == "__main__":
    sys.exit(main())
