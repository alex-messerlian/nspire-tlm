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

Correctness of an ANSWER also requires the STRUCTURAL SHAPE CHECK (docs/ARCHITECTURE.md s6):
  shape                -- the call IS the record's relation under the supplied bindings [FULL doc]

Provenance and shape are not redundant and the mgh case proves it. With the constant inlined,
`eval((2.0)*(5.0))` against `U=m*g*h` is provenance-CLEAN -- 2.0 and 5.0 both trace to the question
-- and shape-MISMATCH, because the relation needs 9.81 and the call does not use it. Provenance
checks that arguments trace to supplied values; shape checks that the OPERATION is the specified one.

THREE-VALUED, and answer_ok folds in only two of the three. `mismatch` disqualifies. `unchecked` does
NOT -- there is no shape rule for diff/integ/evalat/stat yet, and failing every legitimate call to
those would measure the rule's coverage rather than the model. But `unchecked` is NOT a pass either:
callers MUST report shape_counts() alongside any answer figure, so a rising unchecked fraction is
visible instead of quietly inflating the numerator. Same discipline as D3/D4 being reported on their
own line rather than folded into the refusal score.

Does NOT verify: that the tool chosen was the right tool, that the RELATION is physically correct,
or that the prose is true. It checks numeric provenance, structural shape, and form."""
import pathlib as _pathlib
import re, subprocess

REF  = re.compile(r"\bcannot\b|\bcan't\b|\bnot given\b|\bnot enough\b|\bmissing\b|\bdoes not apply\b", re.I)
WF   = re.compile(r"<a>.*<end>", re.S)
NUM  = re.compile(r"-?\d+\.?\d*(?:[eE][-+]?\d+)?")
# Absolute, resolved against THIS file. A relative path made the grader depend on the caller's
# working directory, so provenance silently failed for any launcher not started from the repo root.
PROV = str(_pathlib.Path(__file__).resolve().parent / "provcli")
SHAPE = str(_pathlib.Path(__file__).resolve().parent / "shapecli")

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

def shape_status(full_document):
    """FULL DOCUMENT. Returns 'ok' | 'mismatch' | 'unchecked'. Raises if the binary is missing --
    a check that silently degrades to 'ok' when it cannot run is the defect this repo keeps finding.
    Build it with `make tests`."""
    r = subprocess.run([SHAPE], input=full_document, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(f"shapecli failed: {r.stderr[-200:]}")
    m = re.match(r"shape=(\w+)", r.stdout)
    if not m:
        raise RuntimeError(f"shapecli output unparseable: {r.stdout!r}")
    return m.group(1)

def shape_ok(full_document):
    """Not-disqualified, which is not the same as verified. See the module docstring."""
    return shape_status(full_document) != "mismatch"

def refusal_ok(prompt, generation):
    return well_formed(generation) and is_refusal(generation)

def declared_lhs_unit(full_document):
    """The unit the record declares for the quantity being solved for, or "" if none applies.

    ONE reader, exported, because test_score.py builds the known-good transcript and this module
    grades it -- and if those two derived the unit separately they would disagree on exactly the
    records where it matters, which is the two-graders defect WIRING_AUDIT.md records. The control
    must build a document the real path produces; it can only do that by asking the same question
    the grader asks.

    Returns "" for a dimensionless record (declared "1"), matching the F3 ruling in dispatch.c:
    "0.25", not "0.25 1", because that is what a physicist writes.
    """
    # \Z, so a record span that runs to END OF STRING is read rather than silently missed. Without
    # it this returned "" on a prompt-only document -- and "" means "no unit is required", so the
    # caller read a PARSE FAILURE as a clean bill of health. That is the dim_gate defect exactly:
    # "cannot check" must not share a value with "checked and nothing needed".
    m = re.search(r"<r>(.*?)(?:<tool>|<a>|\Z)", full_document, re.S)
    if not m: return ""
    parts = m.group(1).rsplit("|", 4)          # units | missing: | condition | fit:  (from the RIGHT)
    if len(parts) != 5: return ""
    lhs = parts[0].split("=", 1)[0].strip()
    for tok in parts[1].split():
        if tok.startswith(lhs + ":"):
            u = tok[len(lhs) + 1:].strip()
            return "" if u in ("", "1") else u
    return ""


def required_answer_unit(full_document):
    """The unit the answer must state: the one the RESULT carries, else the record's declaration.

    THE RESULT SPAN IS THE AUTHORITY, and getting that order wrong is what the first version did.
    It required the record's declared LHS unit unconditionally, and failed 11 CONVERSION items --
    `v=d/t` declares `v:m/s` while the question asks for km/h and the runtime injects
    `<res>79.2 km/h</res>`. The answer correctly says km/h. Under the architecture the runtime
    computed the value and its unit; the record only says what the quantity is. So: read the unit
    off <res> when it has one, and fall back to the declaration only when <res> is bare.

    Falling back matters -- it is the whole defect. `<res>12</res>` with a record declaring `v:m/s`
    is precisely the 81% case where the answer said "The speed is 12." and no check looked.
    """
    res = re.findall(r"<res>(.*?)</res>", full_document, re.S)
    if res:
        m = re.fullmatch(r"\s*-?[\d.]+(?:[eE][-+]?\d+)?\s+(\S+)\s*", res[-1])
        if m: return m.group(1)
    return declared_lhs_unit(full_document)


def answer_unit_ok(full_document):
    """The answer must state the unit the record declares for the quantity being solved for.

    A10. 81% of shipped answer spans stated a dimensioned result with NO UNIT -- "The kinetic energy
    is 18." -- and nothing here looked, so every one passed. That is the model's training target and
    a student sees it, which makes it a correctness defect in the output rather than a formatting
    one. This check ships in the SAME change as the fix so the two cannot drift; a generator that
    emits units and a grader that does not require them is the format-parity defect again.

    THE SCOPE IS THE FULL DOCUMENT, and it is in the name -- prompt AND generation. The record is in
    the prompt and the answer is in the generation, so neither alone can decide this. Getting that
    wrong in the opposite directions is exactly what test_scope.py exists to pin.

    THREE-VALUED, deliberately. Returns True when the declared unit is present, False when it is
    declared and absent, and True when the record declares NO unit for the LHS -- a record that
    cannot say what unit is required cannot convict an answer of omitting it. Absence of the
    DECLARATION is not absence of the unit, and conflating them would fail every dimensionless
    record, of which there are 8 of 141.
    """
    unit = required_answer_unit(full_document)
    if not unit:
        return True                            # nothing declared, or dimensionless -> bare is right
    seg = full_document.split("<a>", 1)[1] if "<a>" in full_document else ""
    if not seg: return True
    # A REFUSAL STATES NO RESULT, so there is nothing to carry a unit, and the property is
    # vacuously satisfied. Making this self-contained rather than relying on the caller to run a
    # refusal check first is deliberate: answer_ok composes it after `not is_refusal`, but a
    # DIFFERENT caller would get a wrong False on every refusal -- and the project log records that
    # scope-sensitive checks whose correctness depends on what the caller passes are how three
    # graders came to publish incomparable numbers. The property is "a stated numeric result must
    # carry its unit", so no stated result means nothing to check.
    # A STANDALONE NUMBER, not "contains a digit". Two refusals -- "m_1 is not given" -- read as
    # stating a result under the crude test, which is the proxy-predicate error in miniature: the
    # predicate accepted identifiers with digits in them, which the property never meant.
    if not re.search(r"(?<![A-Za-z0-9_])\d", seg): return True
    # The unit must follow a NUMBER, not merely appear somewhere: "Substituting into v=d/t" mentions
    # no unit but "The speed is 12 m/s" does. Matching the bare string anywhere would be a proxy
    # that the `why` clause satisfies for free on any record whose formula contains the letters.
    return re.search(r"\d\s*" + re.escape(unit) + r"(?![A-Za-z0-9_])", seg) is not None


def answer_ok(prompt, generation):
    return (well_formed(generation) and not is_refusal(generation)
            and answer_matches_result(generation) and prov_clean(prompt + generation)
            and shape_ok(prompt + generation)
            and answer_unit_ok(prompt + generation))

# scope regression: the exact failure this module was rewritten to prevent
_REC = "v=d/t | v:m/s d:m t:s | missing:none | constant speed | fit:high"
_P   = f"<q>A sled goes 84 m in 7 s.</q><r>{_REC}"
_G   = "<tool>eval<arg>84/7</arg></tool><res>12</res><a> The speed is 12 m/s.<end>"
_G_NOUNIT = "<tool>eval<arg>84/7</arg></tool><res>12</res><a> The speed is 12.<end>"
assert is_refusal(_REC),        "record contains 'missing:' -- documents the hazard"
assert not is_refusal(_G),      "a correct generation must not read as a refusal"
assert answer_ok(_P, _G),       "a correct answer must pass with prompt/generation split"
# A10 both directions, asserted at import so the check cannot degrade to a constant unnoticed.
assert not answer_unit_ok(_P + _G_NOUNIT), "an answer omitting the declared unit must FAIL"
assert answer_unit_ok(_P + _G),            "an answer stating the declared unit must PASS"
assert answer_unit_ok("<q>x</q><r>e=m*c^2 | e:1 m:kg c:m/s | missing:none | c | fit:high<a>It is 9.<end>"), \
       "a dimensionless record must not require a unit"
# A PROMPT-ONLY document must still yield its unit. This returned "" before \Z was added, and ""
# means "none required", so a parse failure read as a pass.
assert declared_lhs_unit(_P) == "m/s", "a record span ending the string must still be read"

# shape regression: provenance and shape must disagree on the mgh case, or one of them is redundant
_MP = ("<q>A 2.0 kg book sits 5.0 m up. Find its gravitational potential energy. "
       "m = 2.0, h = 5.0, g = 9.81.</q><r>U=m*g*h | U:J m:kg g:m/s^2 h:m | missing:none | "
       "standard conditions | fit:high")
_MW = "<tool>eval<arg>(2.0)*(5.0)</tool><res>10</res><a> U = 10 J.<end>"
_MR = "<tool>eval<arg>(2.0)*(9.81)*(5.0)</tool><res>98.1</res><a> U = 98.1 J.<end>"
assert prov_clean(_MP + _MW),          "the wrong call IS provenance-clean -- that is the whole point"
assert shape_status(_MP + _MW) == "mismatch", "shape must catch what provenance cannot"
assert shape_status(_MP + _MR) == "ok",       "shape must not reject the correct call"
assert not answer_ok(_MP, _MW),        "answer_ok must reject a shape mismatch"
assert answer_ok(_MP, _MR),            "answer_ok must accept the correct call"
assert well_formed("<tool> e<arg> 1</tool><res> 1</res><a> 1.<end>")
assert not well_formed("<tool> e<arg> 1</tool><res> 1</res> 1.<end>")
