#!/usr/bin/env python3
"""PERMANENT GATE: no Python file may define the same top-level name twice.

WHY. A bulk edit to corpus/generate.py re-inserted a 32 KB block and took the file from 1,070 lines
to 1,565 -- 46% duplicated. `_keys` existed at line 531 AND 957; `seen, vs` at 708 AND 1134.

**EVERY FUNCTIONAL CHECK PASSED.** Python takes the later definition, so the corpus generated
correctly, all 43 gates passed and answer_ok was 100%. The only thing in the repo that could see it
was the negative-control meta-gate: five controls reported SURVIVED, because `str.replace` mutated
the FIRST copy while the second was the live one.

That is a very thin thread. A duplicated definition is a syntactic property of the source and does
not need a mutation harness to notice -- this gate makes it a direct check, so the meta-gate is a
second opinion rather than the only opinion.

WHAT IT DOES NOT VERIFY: duplication that is not a top-level def/class/assignment -- a repeated
block inside a function body, or a duplicated dict entry, both of which Python also silently
accepts. Narrowing the claim to top-level names is what makes this precise; the wider check would
need to diff the AST against itself.

A DELIBERATE REDEFINITION IS RARE AND MUST BE NAMED. There is an allowlist, and it is empty.
"""
import ast, pathlib, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
SCAN = ["corpus", "tools/eval", "train"]
# (file, name) pairs redefined ON PURPOSE. A RATCHET: each needs a reason, and the list should
# shrink. Both entries below are a script narrowing its own working set -- `EVAL = EVAL[:PROBE_N]`
# -- which is ordinary Python that happens to use an UPPER_CASE name. Note both files are in the
# superseded-by-retrain set (docs/RESULT_STRATUM_DRIFT.md), so they are not being edited.
ALLOW: set = {
    ("train/shippability.py", "EVAL"),
    ("train/seed_population.py", "EVAL"),
}


def top_level_names(tree):
    """Definitions and module CONSTANTS. Not every rebinding.

    Rebinding a lowercase module-level variable is ordinary Python in a script -- `rows`, `data`,
    `ok` are reused all over tools/eval and train, legitimately. Flagging those would make this gate
    noise, and a noisy gate is one people stop reading, which is a correctness property (the project log).
    The defect this exists for is a DUPLICATED BLOCK, and a block carries defs, classes and
    UPPER_CASE constants -- so those are what it watches.

    It caught a real one on its first run beyond the duplication: corpus/generate.py still held the
    pre-A6 `ASK = json.load(...)`, the unfiltered 44 frames, shadowed 45 lines later by the reviewed
    7. Behaviour was correct and the file read as though all 44 were in use."""
    out = []
    for node in tree.body:
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
            out.append(node.name)
        elif isinstance(node, ast.Assign):
            for t in node.targets:
                if isinstance(t, ast.Name) and (t.id.isupper() or t.id.lstrip("_").isupper()):
                    out.append(t.id)
    return out


if __name__ == "__main__":
    files = sorted(p for d in SCAN for p in (ROOT / d).rglob("*.py"))
    if not files:
        # ABSENCE IS FAILURE: nothing scanned would otherwise report a clean run.
        print(f"CANNOT CHECK: no Python files under {SCAN}"); sys.exit(2)

    bad, scanned = [], 0
    for p in files:
        try:
            tree = ast.parse(p.read_text())
        except SyntaxError as e:
            print(f"CANNOT CHECK: {p.relative_to(ROOT)} does not parse: {e}"); sys.exit(2)
        scanned += 1
        seen = {}
        for name in top_level_names(tree):
            rel = str(p.relative_to(ROOT))
            if name in seen and (rel, name) not in ALLOW:
                bad.append((rel, name))
            seen[name] = True

    print(f"  python files scanned         {scanned}")
    print(f"  DUPLICATE TOP-LEVEL NAMES    {len(bad)}")
    for rel, name in bad[:12]:
        print(f"      {rel}  redefines {name!r}")

    # POSITIVE CONTROL in-run: the check must be able to fire.
    probe = ast.parse("def f():\n    pass\ndef f():\n    pass\n")
    if len(top_level_names(probe)) != 2 or len(set(top_level_names(probe))) != 1:
        print("\n  FAIL: the positive control did not register a redefinition."); sys.exit(1)
    print("  positive control             a redefined function IS detected")

    if bad:
        print("\n  FAIL: a name is defined twice at module level. Python takes the later one, so")
        print("  the file runs correctly and the duplicate is invisible to every functional check.")
        print("  A bulk edit doubled corpus/generate.py this way and only the mutation meta-gate")
        print("  noticed -- because str.replace hit the first copy while the second was live.")
        sys.exit(1)
    print("\n  PASS: no module defines a top-level name twice")
