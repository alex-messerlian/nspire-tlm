#!/usr/bin/env python3
"""EVERY GATE MUST HAVE A NEGATIVE CONTROL, and this is the gate that enforces it.

Three instances in one session of a check that could not fail: four store suites running committed
binaries no rule rebuilt, and a parity gate whose negative control PASSED with the fix reverted
because the gate carried its own copy of the rule. A gate with no registered control is a gate
nobody has proved can fail, and this repo's history says that is the default state, not the
exception -- 7 of 197 recorded diagnoses had a test that catches a revert.

CONTRACT. Every check listed in tools/eval/run_gates.sh must appear in CONTROLS below with a
mutation: a (file, find, replace) that reverts the behaviour the gate exists to protect. Running
this applies each mutation in isolation and requires that gate to FAIL. A gate with no entry is a
FAILURE here, not a skip -- that is the whole point, and it is why the roster is derived from
run_gates.sh rather than written by hand.

Slow by construction: it rebuilds per mutation. Run it manually and in CI, not inside run_gates.sh,
which would make the suite call itself. Meta-check, like gate_mutation.py and positive_control.py.

  python3 tools/eval/gate_controls.py            every gate
  python3 tools/eval/gate_controls.py NAME ...   just these
"""
import os, pathlib, re, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
os.chdir(ROOT)

# name -> (file, find, replace) | None if the gate is genuinely uncontrollable, with a reason.
CONTROLS = {
    "shape_spec":      ("tools/eval/shape_spec.py",
                        'if op not in ("Mult", "Add"):', 'if False:'),
    "shape_mutation":  ("src/store/shapecheck.c",
                        "if (nodecmp(cw, cg) == 0) return TLM_SHAPE_OK;", "if (1) return TLM_SHAPE_OK;"),
    "test_shapecheck": ("src/store/shapecheck.c",
                        "if (nodecmp(cw, cg) == 0) return TLM_SHAPE_OK;", "if (1) return TLM_SHAPE_OK;"),
    # asmcli, not provcli: `make` has a BUILT-IN `%: %.c` rule, and tools/eval/provcli.c sits beside
    # tools/eval/provcli, so removing the explicit rule still leaves an implicit one and the gate
    # (correctly) reports a rule exists. build/asmcli's source is src/store/asmcli.c, a different
    # directory, so no implicit rule reaches it -- which makes it the target that actually tests the
    # gate. The limitation is real and documented in gate_binaries.py: this gate detects NO RULE, not
    # "only an implicit rule that would build it wrong".
    "gate_binaries":   ("Makefile",
                        "$(BUILD)/asmcli: src/store/asmcli.c", "$(BUILD)/asmcli_DISABLED:"),
    "test_score":      ("tools/eval/score.py",
                        '                 and r["prov_clean"] is True and r["shape"] != "mismatch")',
                        "                 )"),
    "test_prov":       ("tools/eval/provenance.c",
                        "int prov_call_unsourced(const char *doc, double *first) {",
                        "int prov_call_unsourced(const char *doc, double *first) { (void)doc;(void)first; return 0; }\n"
                        "static int _dead(const char *doc, double *first) {"),
    "store_authority": ("corpus/generate.py",
                        '        _uncleaned.append((f, ann.get("name", "")))\n        continue',
                        "        pass"),
    "test_lossmask":   ("train/lossmask.py",
                        "            if tok == res_open:\n                inside = True\n                mask[b, c] = 1",
                        "            if tok == res_open:\n                inside = True"),
    "format_parity":   ("corpus/generate.py",
                        "    order = ([lhs] if lhs in u else []) + [v for v in vs if v != lhs]",
                        "    order = [v for v in vs if v != lhs]"),
    "test_assemble":   ("src/store/assemble.c",
                        'n = appends(out, cap, n, " | fit:high");',
                        'n = appends(out, cap, n, " | fit:high<a>");'),
    "test_toolrun":    ("src/store/toolrun.c",
                        'if (!res[0]) snprintf(res, sizeof res, "!give");',
                        'res[0] = 0;'),
    "test_scope":      ("tools/eval/grade.py",
                        "            and answer_matches_result(generation) and prov_clean(prompt + generation)",
                        "            and answer_matches_result(generation)"),
    "test_genloop":    ("tools/eval/genloop.py",
                        "        lg[res_id] = -1e30", "        pass"),
}

# Gates with no control yet. Listed EXPLICITLY so the count is visible rather than absent.
UNCONTROLLED_REASON = {}

def gates_in_suite():
    txt = pathlib.Path("tools/eval/run_gates.sh").read_text()
    names = set(re.findall(r'printf "  %-20s PASS\\n" "([a-z0-9_]+)"', txt))
    names |= set(re.findall(r'printf "  %-20s PASS\\n" "\$g"', txt) and
                 re.findall(r'^for g in ([a-z0-9_ ]+); do', txt, re.M)[0].split() or [])
    m = re.search(r'^for b in ([a-z0-9_ ]+); do', txt, re.M)
    if m: names |= set(m.group(1).split())
    return sorted(names)

def run_gate(name):
    r = subprocess.run(["bash", "tools/eval/run_gates.sh"], capture_output=True, text=True)
    for line in r.stdout.splitlines():
        parts = line.split()
        if len(parts) >= 2 and parts[0] == name:
            return parts[1]
    return "ABSENT"

def main():
    want = sys.argv[1:]
    roster = gates_in_suite()
    missing = [g for g in roster if g not in CONTROLS and g not in UNCONTROLLED_REASON]
    print(f"gates in run_gates.sh: {len(roster)}   with a registered control: "
          f"{sum(1 for g in roster if CONTROLS.get(g))}   uncontrolled: {len(missing)}")
    if missing:
        print("  NO NEGATIVE CONTROL REGISTERED -- each of these is a gate nobody has proved can fail:")
        for g in missing: print(f"    {g}")

    subprocess.run(["make", "-s", "tests"], capture_output=True)
    failures = list(missing)
    for name in roster:
        spec = CONTROLS.get(name)
        if not spec or spec[1] is None:
            continue
        if want and name not in want:
            continue
        path, find, repl = spec
        f = pathlib.Path(path); original = f.read_text()
        if find not in original:
            print(f"  STALE CONTROL  {name}: the text it mutates is gone from {path}")
            failures.append(name); continue
        try:
            f.write_text(original.replace(find, repl, 1))
            subprocess.run(["make", "-s", "tests"], capture_output=True)
            verdict = run_gate(name)
        finally:
            f.write_text(original)
            subprocess.run(["make", "-s", "tests"], capture_output=True)
        ok = verdict in ("FAIL", "CANNOT")
        print(f"  {'caught  ' if ok else 'SURVIVED'} {name:18} (reverted -> {verdict})")
        if not ok: failures.append(name)

    if failures:
        print(f"\n  {len(failures)} GATE(S) WITHOUT A PROVEN NEGATIVE CONTROL: {sorted(set(failures))}")
        return 1
    print("\n  every registered control fires")
    return 0

if __name__ == "__main__":
    sys.exit(main())
