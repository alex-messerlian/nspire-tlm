#!/usr/bin/env python3
"""PERMANENT GATE: no negative-control mutation may be present in the tree.

TWICE NOW A MUTATED FILE HAS BEEN COMMITTED. First `tools/eval/genloop.py` with the <res> ban
deleted; then `corpus/generate.py` in commit 7d9e88c with A6 reverted -- `rng.choice(ASK_ALL)`,
which restores all 44 mined ASK frames including the 36 sentence fragments, and with them the 22.5%
wrong-quantity defect and a 63% ill-posed rate.

THE RACE, EXACTLY. `gate_controls.py` mutates a file, runs one gate, restores it. Its lock stops a
SECOND gate_controls from racing, and `run_gates.sh` refuses to read the tree while the lock is
held. Neither stops `git add -A`. The suite ran on the clean file, the meta-gate mutated it
milliseconds later, and the commit captured the mutation -- with ALL GATES PASS printed just above
it, truthfully, about a different version of the file.

WHAT MAKES THIS DETECTABLE. Every control declares (file, find, repl). In an unmutated tree `find`
is present. If `find` is ABSENT and `repl` is PRESENT, that control's mutation is live in the tree.
The registry is the oracle -- no guessing, no pattern matching.

WHAT IT DOES NOT VERIFY: a mutation from a harness that is not registered in CONTROLS, or an edit
that coincidentally matches a `repl`. The first is the honest limit; the second would be a false
positive and is why the check requires `find` to be MISSING as well.
"""
import importlib.util, io, contextlib, pathlib, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]

if __name__ == "__main__":
    spec = importlib.util.spec_from_file_location("gc", ROOT / "tools/eval/gate_controls.py")
    gc = importlib.util.module_from_spec(spec)
    # gate_controls takes an exclusive lock at import. Read its source instead of executing it.
    src = (ROOT / "tools/eval/gate_controls.py").read_text()
    ns: dict = {}
    start = src.index("CONTROLS = {")
    end = src.index("\n}\n", start) + 3
    try:
        exec(compile(src[start:end], "<controls>", "exec"), {"__builtins__": {}}, ns)
    except Exception as e:
        print(f"CANNOT CHECK: could not read the CONTROLS registry: {e}"); sys.exit(2)
    controls = ns.get("CONTROLS") or {}
    if not controls:
        # ABSENCE IS FAILURE: an empty registry would report a clean tree for every file.
        print("CANNOT CHECK: the CONTROLS registry parsed empty"); sys.exit(2)

    live, checked = [], 0
    for name, spec_ in controls.items():
        path, find, repl = spec_[0], spec_[1], spec_[2]
        f = ROOT / path
        if not f.exists():
            print(f"CANNOT CHECK: {path} is missing, and control {name!r} targets it"); sys.exit(2)
        text = f.read_text()
        checked += 1
        if find not in text and repl and repl in text:
            live.append((name, path))

    print(f"  controls checked             {checked}")
    print(f"  MUTATIONS LIVE IN THE TREE   {len(live)}")
    for name, path in live[:8]:
        print(f"      {path}  carries control {name!r}'s mutation")

    # POSITIVE CONTROL in-run: the detector must fire on a known-mutated text.
    probe_find, probe_repl, probe_text = "GOOD_LINE", "BAD_LINE", "x = BAD_LINE\n"
    if not (probe_find not in probe_text and probe_repl in probe_text):
        print("\n  FAIL: the positive control did not register a mutation."); sys.exit(1)
    print("  positive control             a live mutation IS detected")

    if live:
        print("\n  FAIL: a negative-control mutation is in the working tree. It will be committed by")
        print("  `git add -A`, and the gate suite may have PASSED moments earlier on the clean file.")
        print("  Restore it (git checkout -- <path>) before committing. This has shipped twice.")
        sys.exit(1)
    print("\n  PASS: no control mutation is present")
