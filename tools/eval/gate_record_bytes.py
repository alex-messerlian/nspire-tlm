#!/usr/bin/env python3
"""PERMANENT GATE: the record span the generator writes must be BYTE-IDENTICAL to the device's.

WHY THIS AND NOT ANOTHER FIELD COMPARISON. The same defect has now been found FOUR TIMES, one field
further over each time, by four different checks that each compared one thing:

    A8   the UNITS field omitted the LHS symbol on 100% of fit:low records
    A9   the CONDITION field disagreed with assemble.c on 57.4% of records
    A11  the FIT token labelled D2 `fit:low`, a shape the device never emits -- 5.14%
    A19  the MISSING field said `none` while the record's variables were unbound -- 166 of 166 D2
         documents against 0 of 2,424 answerable ones, a PERFECT classifier

Each fix was correct and each left the next field exposed. `gate_format_parity` compares four fields
and passed A19, because it compares the units field as a SET and the others for a record the
generator chose -- and it excused a units ORDER difference on 53.6% of records as cosmetic.

So this stops comparing fields. It asks `build/asmcli` -- THE SHIPPED ASSEMBLER -- for the prompt it
would build from the same record and the same entered values, and requires the record span to match
byte for byte. Any field, any order, any spacing, any token: one check, and it cannot be outflanked
by a field nobody thought of.

WHAT IT DOES NOT VERIFY: the QUESTION and the ANSWER. The device does not author those, so there is
nothing to compare them against; they are other gates' problem.
"""
import importlib.util, io, contextlib, json, pathlib, re, subprocess, sys
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / 'corpus'))
from recfmt import formula as _rf_formula  # " | " is the separator; a formula may contain a bare pipe

ROOT = pathlib.Path(__file__).resolve().parents[2]
ASM, STORE = ROOT / "build/asmcli", ROOT / "build/store.tns"
N, SEED, SAMPLE = 1500, 4242, 400


def record_span(doc):
    m = re.search(r"<r>(.*?)(?:<tool>|<a>)", doc, re.S)
    return m.group(1) if m else None


if __name__ == "__main__":
    if not (ASM.exists() and STORE.exists()):
        print(f"CANNOT CHECK: need {ASM} and {STORE}  (make device / tools/store_pack.py)")
        sys.exit(2)
    spec = importlib.util.spec_from_file_location("gen", ROOT / "corpus/generate.py")
    m = importlib.util.module_from_spec(spec)
    try:
        with contextlib.redirect_stdout(io.StringIO()):
            spec.loader.exec_module(m)
    except SystemExit:
        pass
    rid = {r["f"]: r["rid"] for r in json.load(open(ROOT / "corpus/store_clean.json"))}

    out = m.gen(N, seed=SEED)
    docs = [d["text"] if isinstance(d, dict) else d for d in (out[0] if isinstance(out, tuple) else out)]

    cases, lines = [], []
    for t in docs:
        span = record_span(t)
        if span is None or span.strip().startswith("none |"):
            continue                      # the no-record shape has no record to ask about
        f = _rf_formula(span)
        if f not in rid: continue
        q = re.search(r"<q>(.*?)</q>", t)
        if not q: continue
        vals = ",".join(f"{a}={b}" for a, b in re.findall(
            r"([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(-?[\d.]+(?:[eE][-+]?\d+)?)", q.group(1)))
        cases.append((f, span))
        lines.append(f"{rid[f]}\tQ\t{vals}")
        if len(cases) >= SAMPLE: break

    if not cases:
        # ABSENCE IS FAILURE: no comparable document would otherwise report a clean run.
        print("CANNOT CHECK: no generated document carried a store record"); sys.exit(2)

    p = subprocess.run([str(ASM), str(STORE)], input="\n".join(lines) + "\n",
                       capture_output=True, text=True, cwd=ROOT)
    got = [l for l in p.stdout.split("\n") if l.strip()]
    if len(got) != len(cases):
        print(f"CANNOT CHECK: asmcli returned {len(got)} prompts for {len(cases)} records")
        sys.exit(2)

    bad = []
    for (f, mine), line in zip(cases, got):
        theirs = record_span(line + "<a>")
        if theirs is None: theirs = line.split("<r>", 1)[-1]
        if mine.strip() != theirs.strip():
            bad.append((f, mine.strip(), theirs.strip()))

    print(f"  record spans compared        {len(cases)}")
    print(f"  BYTE MISMATCHES              {len(bad)}  ({len(bad)/len(cases):.1%})")
    for f, a, b in bad[:5]:
        print(f"      {f[:34]}\n        corpus {a[:110]}\n        device {b[:110]}")
    if bad:
        print("\n  FAIL: the corpus writes a record span the device does not. The model would train")
        print("  on a prompt it never sees at inference. This has now been found four times in four")
        print("  different fields; comparing bytes is the formulation that cannot be outflanked.")
        sys.exit(1)
    print("\n  PASS: every record span is byte-identical to the shipped assembler's")
