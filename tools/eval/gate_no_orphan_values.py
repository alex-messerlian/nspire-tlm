#!/usr/bin/env python3
"""PERMANENT GATE: every value the tool call uses must appear in the model's own context.

THE PROPERTY, stated so the predicate can be judged against it: at decode time the model sees the
question and the record and nothing else. A numeric literal it emits in <arg> that appears in
neither was RECALLED, not read. TOOL_SPEC exists precisely so the model reads numbers instead of
recalling them, so a corpus that does this trains against the architecture.

WHAT WAS WRONG. 27 of 141 records carry a resolved physical constant. generate.py excluded those
from the question's givens (`free = [v for v in vs if _const_for(r, v) is None]`) and the record
span has no slot for them, but the call used the value. Measured: 16.2% of documents, 4,000-doc
sample.

WHY NOTHING CAUGHT IT -- and this is the part worth keeping. The defect made itself ungradeable:

    constant absent (as shipped)  g=9.81  -> prov_clean False, shape UNCHECKED
    constant absent (as shipped)  g=9810  -> prov_clean False, shape UNCHECKED
    constant inlined              g=9.81  -> prov_clean True,  shape ok
    constant inlined              g=9810  -> prov_clean False, shape MISMATCH

Correct and FABRICATED were indistinguishable to every grader in the repo. grade.py's docstring
says the shape check exists for this case -- "the mgh case proves it" -- and it degraded to
`unchecked` on exactly the mgh case. "Cannot check" scoring as "nothing wrong" is the same defect
the project log records for dim_gate on absent input.

THE PREDICATE IS THE PROPERTY, not a proxy for it: parse every numeric literal out of <arg>, parse
every numeric literal out of the text preceding <tool>, and compare as FLOATS with a relative
tolerance. String comparison would be the proxy -- "2" and "2.0" are the same value, and an earlier
version of this check reported 82% by conflating formatting with absence. That instrument was wrong
before the artefact was.

WHAT IT DOES NOT VERIFY: that the value is CORRECT (g = 9.81 not 9.8), that it is physically
plausible, or that it is bound to the right variable. Those need other checks; docs/CORPUS_PLAN.md
records that numeric plausibility currently has no owner.
"""
import importlib.util, pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
N, SEED, LIMIT = 3000, 5, 0.005
NUM = re.compile(r"-?\d+\.?\d*(?:[eE][-+]?\d+)?")


def orphans(doc):
    """Values used by the call that the model could not have read. Empty when the document has no
    call -- but a document set with NO calls at all is a failure, checked by the caller."""
    if "<tool>" not in doc: return None
    arg = re.search(r"<arg>(.*?)</tool>", doc, re.S)
    if not arg: return None
    context = {float(x) for x in NUM.findall(doc[:doc.index("<tool>")])}
    used    = {float(x) for x in NUM.findall(arg.group(1))}
    return {v for v in used
            if not any(abs(v - c) <= 1e-9 * max(1.0, abs(v)) for c in context)}


if __name__ == "__main__":
    spec = importlib.util.spec_from_file_location("gen", ROOT / "corpus/generate.py")
    m = importlib.util.module_from_spec(spec)
    try: spec.loader.exec_module(m)
    except SystemExit: pass
    out = m.gen(N, seed=SEED)
    docs = out[0] if isinstance(out, tuple) else out
    texts = [d["text"] if isinstance(d, dict) else d for d in docs]

    results = [(t, orphans(t)) for t in texts]
    withcall = [(t, o) for t, o in results if o is not None]
    if not withcall:
        # ABSENCE IS FAILURE: a corpus with no tool calls would otherwise score a perfect zero.
        print("CANNOT CHECK: no document contained a tool call"); sys.exit(2)

    bad = [(t, o) for t, o in withcall if o]
    rate = len(bad) / len(withcall)
    print(f"  documents with a call        {len(withcall):,} of {len(texts):,}")
    print(f"  CALL USES AN UNSEEN VALUE    {len(bad):,}  ({rate:.2%}, limit {LIMIT:.2%})")
    for t, o in bad[:4]:
        print(f"      orphan {sorted(o)}  {t[:96]}")

    # POSITIVE CONTROL, in-run: a check that cannot fire is not a check, and this one is cheap
    # enough to prove on every run rather than only under gate_controls.
    planted = ("<q>Determine U. Given m = 2, h = 10.</q><r>U=m*g*h | U:J m:kg g:m/s^2 h:m"
               "<tool>eval<arg>((2.0))*(9.81)*((10.0))</tool><res>196.2</res><a>x<end>")
    if not orphans(planted):
        print("\n  FAIL: the positive control was not caught -- this check is not working.")
        sys.exit(1)
    print(f"  positive control             caught (9.81 absent from context)")

    if rate > LIMIT:
        print(f"\n  FAIL: {rate:.2%} of calls use a value the model never saw. It was RECALLED, not")
        print( "  read, which is the architecture inverted -- and while the value is absent the")
        print( "  graders cannot tell a correct constant from a fabricated one (both 'unchecked').")
        sys.exit(1)
    print("\n  PASS: every value in every call appears in the question or the record")
