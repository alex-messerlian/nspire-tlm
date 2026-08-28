#!/usr/bin/env python3
"""PERMANENT GATE: nothing may hand-roll mutate-and-restore on a build source.

WHY. Three independent implementations of "temporarily rewrite a source file and put it back" each
lost the TIMESTAMP while correctly restoring the bytes:

  gate_controls.py per-control ..... fixed, WITH a comment naming the hazard
  gate_controls.py interrupt path .. same bug, same file, same day
  shape_mutation.py ................ same bug again, and it fired on EVERY ordinary green run,
                                     silently invalidating the cross-compiled device build

The comment on the first fix did not prevent the second or the third, because a comment does not
propagate to the next implementation. All three now use tools/eval/mutatectx.py, and this gate
exists so a fourth is a failure rather than a discovery.

THE PROPERTY, stated narrowly enough to be true: a file that WRITES BACK CONTENT IT READ EARLIER
-- that is, RESTORES a source it temporarily changed -- must go through mutatectx.

Writing a source file is not itself the hazard, and an earlier version of this gate got that
wrong: it flagged four CODE GENERATORS (gen_exp_table, mkfont, gen_panel_tokens, snap_greys) that
edit src/ permanently, where advancing the mtime is CORRECT because the file really did change.
Three of the four also read the file first, so "reads and writes" does not separate them either.
The distinguishing act is restoring: `original = SRC.read_text()` ... `SRC.write_text(original)`,
which leaves the bytes as they were and the timestamp as they were not.

That same version also MISSED all three files it was written for, because their writes now go
through the helper and it was looking for `.write_text`. Wrong in both directions at once -- the
proxy-predicate pattern, in the gate written to end an instance of the proxy-predicate pattern.

THE PREDICATE IS AN AST IMPORT CHECK, NOT A SUBSTRING. `"mutatectx" in source` would be satisfied
by a comment, a local variable, or a function somebody named `mutatectx` -- which is precisely how
test_genloop.py's exemption was defeated: it accepted any file containing the string "genloop", and
a file that reimplemented the loop and named its copy `genloop()` sailed through. Only a real
`from mutatectx import ...` / `import mutatectx` counts here.
"""
import ast, pathlib, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
SCAN = ("tools/", "train/", "corpus/", "bench/")
WRITE_ATTRS = {"write_text", "writelines"}


def tracked_python():
    out = subprocess.run(["git", "ls-files", "*.py"], cwd=ROOT,
                         capture_output=True, text=True).stdout.split()
    return [p for p in out if p.startswith(SCAN)]


def imports_helper(tree):
    for n in ast.walk(tree):
        if isinstance(n, ast.ImportFrom) and (n.module or "").split(".")[-1] == "mutatectx":
            return True
        if isinstance(n, ast.Import) and any(a.name.split(".")[-1] == "mutatectx" for a in n.names):
            return True
    return False


def restores(tree):
    """Names that are read from a file and later written back to one -- i.e. restored.

    `orig = P.read_text()` binds orig to file content; `P.write_text(orig)` puts it back
    unchanged. That pair is the mutate-and-restore signature, and it is what needs the timestamp
    guarantee. A generator writes a value it COMPUTED, so its written name is not a read name.
    """
    # A NAME THAT IS REASSIGNED IS AN EDIT, NOT A RESTORE. gen_panel_tokens.py and snap_greys.py
    # do `src = SRC.read_text()` then `src = src[:i] + new + src[j:]` then write -- they write
    # MODIFIED content, so the file genuinely changed and advancing its mtime is correct. Counting
    # assignments per name separates "put back what I read" from "read, change, save".
    assigned = {}
    for n in ast.walk(tree):
        if isinstance(n, ast.Assign):
            for t in n.targets:
                if isinstance(t, ast.Name):
                    assigned[t.id] = assigned.get(t.id, 0) + 1
    read_names = set()
    for n in ast.walk(tree):
        if isinstance(n, ast.Assign) and isinstance(n.value, ast.Call):
            fn = n.value.func
            is_read = (isinstance(fn, ast.Attribute) and fn.attr == "read_text") or (
                isinstance(fn, ast.Attribute) and fn.attr == "read"
                and isinstance(fn.value, ast.Call))
            if is_read:
                for t in n.targets:
                    if isinstance(t, ast.Name) and assigned.get(t.id, 0) == 1:
                        read_names.add(t.id)
    written_back = set()
    for n in ast.walk(tree):
        if isinstance(n, ast.Call) and isinstance(n.func, ast.Attribute) \
           and n.func.attr in WRITE_ATTRS and n.args:
            a = n.args[0]
            if isinstance(a, ast.Name) and a.id in read_names:
                written_back.add(a.id)
    return written_back


def src_targets(tree):
    """Any C source literal, not only those under src/.

    Scoped to src/ at first, which left tools/eval/prov_mutation.py -- a real mutator of
    tools/eval/provenance.c, the file every grader's provcli is built from -- uncounted. A gate
    that under-reports its own coverage invites the reader to conclude more is checked than is."""
    return {n.value for n in ast.walk(tree)
            if isinstance(n, ast.Constant) and isinstance(n.value, str)
            and n.value.endswith((".c", ".h")) and "/" in n.value}


# TWO SAMPLES THE GATE CLASSIFIES ON EVERY RUN, because on a clean tree there are no offenders
# and therefore NOTHING EXERCISES THE DETECTION. The negative control proved it: breaking the
# offender branch outright left the gate reporting PASS, since a disabled check and a check with
# nothing to find are the same output. A control surviving is the finding -- the suite lacked the
# case, so here is the case, permanently.
_BAD = '''
import pathlib, subprocess
SRC = pathlib.Path("src/store/shapecheck.c")
def main():
    original = SRC.read_text()
    try:
        SRC.write_text(original.replace("a", "b", 1))
    finally:
        SRC.write_text(original)
'''
_GOOD = '''
import pathlib
OUT = pathlib.Path("src/store/app.c")
def main():
    src = OUT.read_text()
    src = src[:10] + "generated" + src[20:]
    OUT.write_text(src)
'''


def classify(tree):
    """'offender' | 'user' | None -- the single decision, so one path is under test."""
    back, targets = restores(tree), src_targets(tree)
    if imports_helper(tree) and targets:
        return "user", sorted(targets)
    if back:
        return "offender", sorted(back)
    return None, []


def self_check():
    """Fail loudly if the predicate has stopped discriminating. Positive AND negative."""
    bad, _ = classify(ast.parse(_BAD))
    good, _ = classify(ast.parse(_GOOD))
    problems = []
    if bad != "offender":
        problems.append(f"a hand-rolled mutate-and-restore classified as {bad!r}, not 'offender'")
    if good is not None:
        problems.append(f"a code generator that edits in place classified as {good!r}, not clean")
    return problems


def main():
    problems = self_check()
    if problems:
        print("  CANNOT CHECK: this gate's own predicate no longer discriminates:")
        for p_ in problems: print(f"    - {p_}")
        return 2
    print("  self-check: known-bad classifies as offender, known-good as clean")
    files = tracked_python()
    if not files:
        print("  CANNOT CHECK: git ls-files returned no python. Refusing to report a pass.")
        return 2
    offenders, users, scanned = [], [], 0
    for rel in files:
        f = ROOT / rel
        try:    tree = ast.parse(f.read_text())
        except (OSError, SyntaxError):  continue
        scanned += 1
        kind, detail = classify(tree)
        if kind == "user":
            users.append((rel, detail))
        elif kind == "offender":
            offenders.append((rel, detail))
    print(f"  scanned {scanned} tracked python files under {', '.join(SCAN)}")
    for rel, t in users:
        print(f"  ok       {rel}  -> {', '.join(t)}")
    for rel, t in offenders:
        print(f"  OFFENDER {rel}  restores {', '.join(t)} by hand, without mutatectx")
    if offenders:
        print(f"\n  FAIL: {len(offenders)} file(s) mutate-and-restore a source by hand.")
        print("  Restoring the bytes and not the mtime is invisible to git and fatal to make;")
        print("  it shipped three times. Use `with mutating(SRC, rebuild=...)` from mutatectx.")
        return 1
    if not users:
        print("  CANNOT CHECK: no file was found that mutates a build source at all, so this")
        print("  gate proved nothing. That is a change in the codebase, not a pass.")
        return 2
    print(f"\n  PASS: all {len(users)} build-source mutators go through mutatectx")
    return 0


if __name__ == "__main__":
    sys.exit(main())
