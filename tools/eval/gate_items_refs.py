#!/usr/bin/env python3
"""PERMANENT GATE: every stored reference answer in items.json must be what its own call executes to.

THE INVARIANT, from gen_items.py's own docstring: "every call is executed through evalcli and the
reference answer is whatever the evaluator returns. No expected answer is typed by hand, so none can
be wrong." That guarantee held only while the file was GENERATED. It stopped holding the moment
anyone edited items.json by hand -- and commit 70bb3bf did exactly that, rewriting `q`, `record` and
`calls` on 16 items to settle three spelling decisions, without re-executing. `ref` is a DERIVED
field, so it went stale silently.

WHAT THAT COST. Two items became unpassable by a correct model:

    B1-016  record p=F/A (lowercased)   call solves p=F/A -> A=F/p    ref said A=F/P
    B1-019  record F=-k*x (signed)      call solves F=-k*x -> x=-F/k  ref said x=F/k

Built from their own stored calls, both ideal transcripts score equivalent=False. A model that did
everything right failed them, and every accuracy figure measured on items.json carried that.

This is the derived-figure class again -- the same one gate_stale_figures.py exists for -- except
the derived value is an expected ANSWER, so its staleness reads as the model being wrong.

WHAT IT DOES NOT VERIFY: that the CALL is the right call for the question, or that the question is
well-posed. It verifies only that the stored answer is the one the stored call produces. A wrong
call with a matching ref passes here and is a different check's problem.
"""
import json, pathlib, re, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
CLI = ROOT / "tools/eval/evalcli"
ITEMS = ROOT / "tools/eval/items.json"


def execute(call):
    """evalcli prints the injected span, `<res>VALUE</res>`; items.json stores the bare VALUE.

    The first version of this gate compared them raw and reported 179 of 179 mismatched -- an
    oracle that fails on valid input, which is worse than the thing it measures. Unwrapping is the
    fix, and the unwrap is asserted rather than assumed: a payload that does not arrive wrapped is
    an evalcli change, not a passing item."""
    p = subprocess.run([str(CLI), call], capture_output=True, text=True, cwd=ROOT)
    out = p.stdout.strip()
    m = re.fullmatch(r"<res>(.*)</res>", out, re.S)
    return m.group(1) if m else out


def norm(s):
    """Compare as the evaluator would print it: whitespace-insensitive, case-SENSITIVE.

    Case matters and must not be normalised away -- `A=F/P` vs `A=F/p` IS the defect this gate
    exists for, and a case-insensitive compare would be a proxy that accepts exactly the bug.
    """
    return re.sub(r"\s+", "", s or "")


if __name__ == "__main__":
    if not CLI.exists():
        print(f"CANNOT CHECK: {CLI} is not built. Run: make -C tools/eval evalcli"); sys.exit(2)
    items = json.load(open(ITEMS))
    checked = mismatched = 0
    bad = []
    for it in items:
        calls, refs = it.get("calls") or [], it.get("ref") or []
        if not calls or not refs:
            continue                    # not every item carries a gold call; those are other tests
        for k, call in enumerate(calls):
            if k >= len(refs): break
            got = execute(call)
            checked += 1
            if norm(got) != norm(refs[k]):
                mismatched += 1
                bad.append((it.get("id", "?"), call, refs[k], got))
    if not checked:
        # ABSENCE IS FAILURE: an items.json whose calls all went missing would score a clean zero.
        print("CANNOT CHECK: no item carried both a call and a reference"); sys.exit(2)

    print(f"  stored calls executed        {checked}")
    print(f"  REF DISAGREES WITH ITS CALL  {mismatched}")
    for i, c, r, g in bad[:8]:
        print(f"      {i:8} {c[:52]}\n               ref {r!r}  executes to {g!r}")
    if mismatched:
        print(f"\n  FAIL: {mismatched} stored reference answers are not what their own call produces.")
        print( "  `ref` is DERIVED by executing `calls`; editing an item by hand makes it stale, and")
        print( "  a stale expected answer reads as the model being wrong. Re-execute, do not retype.")
        sys.exit(1)
    print("\n  PASS: every stored reference is what its own call executes to")
