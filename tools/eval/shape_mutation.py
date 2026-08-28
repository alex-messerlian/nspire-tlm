#!/usr/bin/env python3
"""Mutation test for the structural call check -- docs/ARCHITECTURE.md section 6.

A suite that passes on the first run is not trusted until a deliberately broken version of the code
has FAILED it. This project has three recorded cases of all-green meaning "measuring nothing":
gate_mutation found holes in gates that passed everything, positive_control found five metrics where
"working" and "measuring nothing" were the same number, and test_loader passed nine negative controls
while a variable line that had lost its unit loaded clean.

Each mutation below removes ONE load-bearing clause of the rule. Every one must make
build/test_shapecheck fail. A mutation that survives means the suite does not cover that clause, and
the clause is therefore free to rot.

Run: python3 tools/eval/shape_mutation.py     (exit 0 = every mutation was caught)
"""
import os
import pathlib, re, subprocess, sys
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from mutatectx import mutating

SRC = pathlib.Path("src/store/shapecheck.c")
BUILD = [
    "cc", "-O2", "-DTLM_HOST", "-Isrc/store", "-Itools/eval",
    "-o", "build/test_shapecheck_mut", "tools/eval/test_shapecheck.c", "src/store/shapecheck.c",
    *[f"tools/eval/{f}.c" for f in
      "fmt ast units parser numeric deriv solve literal integrate stat dispatch".split()],
    "-lm",
]

MUTATIONS = [
    ("non-commutativity of / and - is removed",
     'if (op not in) ...',   # marker only; the real edit is below
     'if (n->t == N_MUL || n->t == N_ADD) {',
     'if (n->t == N_MUL || n->t == N_ADD || n->t == N_DIV || n->t == N_SUB) {'),

    ("commutativity is removed -- sorting disabled",
     'insertion sort',
     'while (j >= 0 && nodecmp(ch[j], k) > 0) { ch[j+1] = ch[j]; j--; }',
     'while (0) { ch[j+1] = ch[j]; j--; }'),

    ("the unbound-variable path returns OK instead of UNCHECKED",
     'unbound -> UNCHECKED',
     '"correct call, so the call cannot be checked", unbound, formula);\n            return TLM_SHAPE_UNCHECKED;',
     '"correct call, so the call cannot be checked", unbound, formula);\n            return TLM_SHAPE_OK;'),

    ("functions with no shape rule return OK instead of UNCHECKED",
     'no rule -> UNCHECKED',
     'snprintf(why, (size_t)cap, "no shape rule for \'%s\' -- NOT CHECKED, not clean", fn);\n        return TLM_SHAPE_UNCHECKED;',
     'snprintf(why, (size_t)cap, "no shape rule for \'%s\' -- NOT CHECKED, not clean", fn);\n        return TLM_SHAPE_OK;'),

    ("the numeric tolerance is widened so 9.8 passes for 9.81",
     'tolerance',
     'return fabs(a - b) / m < 1e-9;',
     'return fabs(a - b) / m < 1e-1;'),

    ("operand comparison is removed",
     'operand multiset',
     'if (!leaf_eq(&lw[i], &lg[j])) ;\n',   # unused marker
     None),
]


def run_build_and_test(m, source_text):
    m.write(source_text)
    b = subprocess.run(BUILD, capture_output=True, text=True)
    if b.returncode != 0:
        return "BUILD-FAILED", b.stderr[-300:]
    t = subprocess.run(["./build/test_shapecheck_mut"], capture_output=True, text=True)
    return ("PASS" if t.returncode == 0 else "FAIL"), t.stdout.strip().splitlines()[-1]


def main():
    # THE MUTATE-AND-RESTORE CONTRACT LIVES IN mutatectx, not here. This file is the third place
    # the same defect appeared -- content restored, mtime left at "now" -- and the only one that
    # fired on an ordinary green run, silently invalidating the cross-compiled device build on
    # every gate suite invocation. See tools/eval/mutatectx.py for the measurement.
    original = SRC.read_text()
    # Sanity first: the unmutated source must PASS, or every "caught" below is meaningless.
    survived = []
    with mutating(SRC, rebuild=lambda: subprocess.run(BUILD, capture_output=True)) as m:
        status, tail = run_build_and_test(m, original)
        if status != "PASS":
            print(f"  ABORT: the UNMUTATED source does not pass ({status}). {tail}")
            return 1
        print(f"  baseline: unmutated source PASSES -- {tail}")

        for label, _marker, old, new in MUTATIONS:
            if new is None:
                continue
            if old not in original:
                survived.append((label, "MUTATION DID NOT APPLY -- the code it edits has moved"))
                print(f"  SKIP  {label}\n        the text it patches is not in the source any more")
                continue
            status, tail = run_build_and_test(m, original.replace(old, new, 1))
            caught = status in ("FAIL", "BUILD-FAILED")
            print(f"  {'caught ' if caught else 'SURVIVED'} {label}")
            if not caught:
                survived.append((label, tail))
            else:
                print(f"          -> {status}: {tail}")

    if survived:
        print(f"\n  {len(survived)} MUTATION(S) SURVIVED -- the suite does not cover these clauses:")
        for label, tail in survived:
            print(f"    * {label}: {tail}")
        return 1
    print(f"\n  all {sum(1 for m in MUTATIONS if m[3] is not None)} mutations caught")
    return 0


if __name__ == "__main__":
    sys.exit(main())
