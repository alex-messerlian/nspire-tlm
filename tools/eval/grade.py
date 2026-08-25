"""THE grader. One definition, imported by everything that scores a generated document.

SCOPE IS PART OF THE CONTRACT, and getting it wrong produced a published artifact:

  * the refusal regex contains `\\bmissing\\b`, and EVERY record carries a `missing:` field, so
    matching it against a full document marks a perfect answer as a refusal;
  * provenance needs the question and the record to source numbers, so matching it against the
    generation alone marks a correct answer as unsourced.

No single string satisfies both. Callers therefore pass `prompt` and `generation` SEPARATELY and
this module applies each check to the right scope. Before this fix three callers each chose a
different scope and silently measured three different things -- fit_catch scored 100% refusal on
both arms purely because `missing:` appears in every record.

Correctness of an ANSWER requires all four, and they are not redundant:
  well_formed          -- bounded <a>..<end>, balanced tool spans          [generation]
  not a refusal        -- same pattern used to score the refusal side      [generation]
  result-match         -- stated number traces to the injected <res>       [generation]
  provenance-clean     -- no invented answer numbers, no invented args     [FULL document]

Does NOT verify: that the tool chosen was the right tool, that the formula applied was the right
formula, or that the prose is true. It checks numeric provenance and shape."""
import pathlib as _pathlib
import re, subprocess

REF  = re.compile(r"\bcannot\b|\bcan't\b|\bnot given\b|\bnot enough\b|\bmissing\b|\bdoes not apply\b", re.I)
WF   = re.compile(r"<a>.*<end>", re.S)
NUM  = re.compile(r"-?\d+\.?\d*(?:[eE][-+]?\d+)?")
# Absolute, resolved against THIS file. A relative path made the grader depend on the caller's
# working directory, so provenance silently failed for any launcher not started from the repo root.
PROV = str(_pathlib.Path(__file__).resolve().parent / "provcli")

def well_formed(generation):
    o = generation
    if not WF.search(o): return False
    if o.count("<tool>") != o.count("</tool>"): return False
    return not (o.count("<tool>") and o.count("<arg>") < o.count("<tool>"))

def is_refusal(generation):
    """GENERATION ONLY. Never pass a full document: `missing:` in the record always matches."""
    return bool(REF.search(generation))

def _sig_round(v, sig):
    if v == 0: return 0.0
    import math as _m
    mgn = 10.0 ** (sig - 1 - _m.floor(_m.log10(abs(v))))
    return _m.floor(abs(v) * mgn + 0.5) / mgn * (1 if v > 0 else -1)

def answer_matches_result(generation):
    o = generation
    res = re.findall(r"<res>(.*?)</res>", o, re.S)
    if not res: return False
    seg = o.split("<a>", 1)[1] if "<a>" in o else ""
    vals = []
    for tok in NUM.findall(seg):
        d = sum(1 for ch in tok.split("e")[0] if ch.isdigit())
        try: vals.append((float(tok), max(1, d)))
        except ValueError: pass
    refs = []
    for r in res:
        mm = NUM.search(r)
        if mm:
            try: refs.append(float(mm.group()))
            except ValueError: pass
    return any(_sig_round(rv, sg) == _sig_round(v, sg) for v, sg in vals for rv in refs)

def prov_clean(full_document):
    """FULL DOCUMENT (prompt + generation). Provenance sources numbers to the question and record;
    given the generation alone it reports every question-sourced number as invented."""
    r = subprocess.run([PROV], input=full_document, capture_output=True, text=True)
    if r.returncode != 0: raise RuntimeError(f"provcli failed: {r.stderr[-200:]}")
    m = re.findall(r"=(-?\d+)", r.stdout)      # SIGNED: -1 is a sentinel, not a count
    if len(m) != 2: raise RuntimeError(f"provcli output unparseable: {r.stdout!r}")
    a, c = (int(v) for v in m)
    return a == 0 and c == 0                   # sentinel is NOT clean

def refusal_ok(prompt, generation):
    return well_formed(generation) and is_refusal(generation)

def answer_ok(prompt, generation):
    return (well_formed(generation) and not is_refusal(generation)
            and answer_matches_result(generation) and prov_clean(prompt + generation))

# scope regression: the exact failure this module was rewritten to prevent
_REC = "v=d/t | v:m/s d:m t:s | missing:none | constant speed | fit:high"
_P   = f"<q>A sled goes 84 m in 7 s.</q><r>{_REC}"
_G   = "<tool>eval<arg>84/7</arg></tool><res>12</res><a> The speed is 12 m/s.<end>"
assert is_refusal(_REC),        "record contains 'missing:' -- documents the hazard"
assert not is_refusal(_G),      "a correct generation must not read as a refusal"
assert answer_ok(_P, _G),       "a correct answer must pass with prompt/generation split"
assert well_formed("<tool> e<arg> 1</tool><res> 1</res><a> 1.<end>")
assert not well_formed("<tool> e<arg> 1</tool><res> 1</res> 1.<end>")
