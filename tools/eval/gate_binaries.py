#!/usr/bin/env python3
"""No tracked binary may exist without a rule that rebuilds it.

WHY. tools/eval/provcli -- what grade.py, select_run.py and score.py all shell out to, the check the
architecture's central claim rests on -- was a committed binary with NO build rule. A fix to
provenance.c did not reach it, and every provenance number in this repo came from whatever was
compiled at some past moment. The same defect had already been found on four gate binaries
(test_loader/picker/assemble/tokenizer, which could not fail) and on build/asmcli, THE SHIPPED
ASSEMBLER that three measurement harnesses invoke.

Three separate instances of one class, so it gets a gate rather than a third fix.

A binary that is tracked and unbuildable is a measurement of the past presented as the present.
Either give it a rule or stop tracking it.

WHAT THIS DOES NOT DETECT. `make` has a built-in `%: %.c` rule, so a binary sitting beside a .c of
the same name always "has a rule" even with every explicit rule deleted -- and that implicit rule
would compile the wrong thing (provcli without provenance.c, which does not link). This gate
therefore catches NO RULE AT ALL, which is what build/apphost and build/tlmui were, and does not
catch "only an implicit rule". Closing that needs the gate to check the rule builds the right
sources, which is a different and larger check.

Exit 0 clean, 1 on a violation, 2 if it cannot run.
"""
import os, pathlib, re, stat, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
os.chdir(ROOT)

# Binaries that are tracked ON PURPOSE and cannot be built here. Each needs a reason.
EXEMPT = {
    "tools/nspire-cli/nsp": "links libnspire from a sibling checkout; built by tools/nspire-cli/Makefile "
                            "on a machine that has it, and committed so a device pass does not need one",
}

try:
    tracked = subprocess.run(["git", "ls-files"], capture_output=True, text=True, check=True).stdout.split()
except Exception as e:
    print(f"CANNOT CHECK: git ls-files failed: {e}")
    sys.exit(2)

# ASK MAKE, DO NOT REGEX THE MAKEFILE. The first version of this gate pattern-matched `name:` and
# missed `$(addprefix $(BUILD)/,$(TESTS_APP)): $(BUILD)/%:` -- so it reported eight suites that DO
# have a rule. That is the proxy-predicate error, committed inside the gate written to catch a
# proxy-predicate error. `make -n` is not a proxy for "make knows how to build this"; it is the
# question itself.
def has_rule(target):
    for mkdir, mkfile in (("", "Makefile"), ("tools/eval", "Makefile"), ("bench", "Makefile"),
                          ("src", "Makefile"), ("tools/nspire-cli", "Makefile")):
        d = ROOT / mkdir if mkdir else ROOT
        if not (d / mkfile).exists():
            continue
        try:
            rel = os.path.relpath(ROOT / target, d)
        except ValueError:
            continue
        # -B (--always-make) is required. Plain `make -n` on an EXISTING file with no rule says
        # "up to date" or "Nothing to be done", which reads exactly like success -- so the first
        # version of this check reported 0 unbuildable while three genuinely had no rule. -B forces
        # make to print the recipe it WOULD run, and "Nothing to be done" then means there is none.
        r = subprocess.run(["make", "-n", "-B", rel], cwd=d, capture_output=True, text=True)
        out = r.stderr + r.stdout
        if "No rule to make target" in out or "Nothing to be done" in out:
            continue
        if out.strip():
            return True
    return False

MAGIC = (b"\x7fELF", b"\xcf\xfa\xed\xfe", b"\xce\xfa\xed\xfe", b"\xca\xfe\xba\xbe")
offenders = []
checked = 0
for f in tracked:
    p = pathlib.Path(f)
    if not p.exists() or p.suffix in (".py", ".c", ".h", ".md", ".json", ".sh", ".txt",
                                      ".tsv", ".jsonl", ".inc", ".yml"):
        continue
    try:
        head = open(p, "rb").read(4)
    except Exception:
        continue
    if not ((os.stat(p).st_mode & stat.S_IXUSR) or head[:4] in MAGIC):
        continue
    checked += 1
    if f in EXEMPT:
        continue
    if not has_rule(f):
        offenders.append(f)

print(f"tracked binaries: {checked}   exempt: {len(EXEMPT)}   unbuildable: {len(offenders)}")
if offenders:
    print("NO BUILD RULE -- give it one or stop tracking it:")
    for f in offenders:
        print(f"  {f}")
    sys.exit(1)
print("clean")
