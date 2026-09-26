#!/usr/bin/env python3
"""Every test suite the Makefile builds must be EXECUTED by run_gates.sh.

    python3 tools/eval/gate_tests_wired.py

A156 found three suites -- test_askparse, test_autopick, test_ansmatch -- in the Makefile's TESTS_*
lists and in no invocation in run_gates.sh. `make check` built them and ran nothing, so every
assertion in them read as coverage while executing never. test_autopick had been failing since A125.
Same class as provenance.c with no caller: a check that is not in a decision path is not a check.

THE PROPERTY is "run_gates.sh executes ./build/<suite>", not "the name appears in the file": a
comment mentioning a suite would satisfy a substring test (the genloop-naming defeat, again). So
comments are stripped first, and a suite counts as wired only through an executed form -- a
./build/<suite> command or a name in the `for b in ...` loop that runs ./build/$b.

The known-bad sample below is classified on EVERY run, so a broken predicate is visible even when
the real tree has nothing to find (the gate_mutate_helper lesson: a disabled check and a check with
nothing to find print the same PASS).
"""
import pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]


def built_suites(makefile: str) -> set[str]:
    out = set()
    for m in re.finditer(r"^TESTS_(?:APP|EVAL|PLAIN|STORE)\s*:=\s*(.*)$", makefile, re.M):
        out |= set(m.group(1).split())
    return out


def executed_suites(script: str) -> set[str]:
    code = "\n".join(re.split(r"(?<![\"'\\$])#", line, maxsplit=1)[0] for line in script.splitlines())
    out = set(re.findall(r"\./build/(test_\w+)", code))
    for m in re.finditer(r"^\s*for b in ([^;]*); do(.*?)^done", code, re.M | re.S):
        if "./build/$b" in m.group(2):
            out |= set(m.group(1).split())
    return out


def main() -> int:
    # The predicate, exercised on a sample it must flag and a sample it must pass.
    bad = executed_suites("# ./build/test_x mentioned in a comment only\n")
    good = executed_suites('for b in test_y; do\n  "./build/$b"\ndone\n./build/test_x\n')
    if "test_x" in bad or not {"test_x", "test_y"} <= good:
        print("FAIL: the predicate misclassifies its own samples")
        return 1
    built = built_suites((ROOT / "Makefile").read_text())
    run = executed_suites((ROOT / "tools/eval/run_gates.sh").read_text())
    if not built:
        print("FAIL: no TESTS_* lists found in the Makefile -- cannot check, which is a FAIL")
        return 1
    missing = sorted(built - run)
    print(f"  suites built by make tests: {len(built)}; executed by run_gates.sh: "
          f"{len(built & run)}")
    if missing:
        print(f"FAIL: built but never run: {', '.join(missing)}")
        return 1
    print("PASS: every built suite is executed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
