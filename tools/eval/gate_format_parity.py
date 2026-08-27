#!/usr/bin/env python3
"""PROVISIONAL -- superseded by tools/eval/gate_record_bytes.py. See the note below.

The record span the GENERATOR writes must match the one the DEVICE assembles.

> **PROVISIONAL, and kept only as a second opinion.** This gate compares FOUR FIELDS. The same
> defect was then found a fifth time, in `missing:`, and a sixth in the `<res>` span -- because a
> field-comparison gate only ever checks the field somebody had just thought about.
> `gate_record_bytes.py` diffs the WHOLE record span against `build/asmcli` byte for byte and
> subsumes every check here.
>
> **What this covers that bytes do not:** it names WHICH field diverged, which is a better error
> message, and it exercises `gen.units_field()` directly so a control can target that function.
> Neither is a property; both are diagnostics. Delete this gate once gate_record_bytes has run
> clean across a wider sample of records than the 400 it samples today.

THREE PRODUCERS OF ONE FORMAT, and they disagreed. src/store/assemble.c emits the LHS unit on every
device prompt -- "units for every variable, LHS included -- the answer needs a unit to state", and
tools/eval/test_assemble.c asserts it. corpus/generate.py derived its units map from the RIGHT-hand
side only, so 0 of 197,428 shipped training documents carried it. Every prompt the device produces
therefore differed from every document the model was trained on, in the first field after the
formula, and the model had never seen the unit it is supposed to label its answer with.

docs/EXPERIMENT_PLAN.md:295-313 records this exact skew as found and fixed -- "the five-field
skeleton was unified". The skeleton was. The units field was not. A fix that unified the containers
and not the contents, and nothing compared them afterwards.

This compares them. For a sample of store records it builds the span both ways and requires the
VARIABLE SET of the units field to agree. It deliberately does not require byte equality: the
generator orders by the right-hand side and the assembler by store order, and ordering is not the
defect. What is checked is that neither side is missing a variable the other has.

Exit 0 clean, 1 on a mismatch, 2 if it cannot run.
"""
import io, contextlib, importlib.util, json, os, re, subprocess, sys, pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
os.chdir(ROOT)

ASM, STORE = "build/asmcli", "build/store.tns"
if not (pathlib.Path(ASM).exists() and pathlib.Path(STORE).exists()):
    print(f"CANNOT CHECK: need {ASM} and {STORE}")
    sys.exit(2)
try:
    spec = importlib.util.spec_from_file_location("gen", "corpus/generate.py")
    gen = importlib.util.module_from_spec(spec)
    with contextlib.redirect_stdout(io.StringIO()):
        spec.loader.exec_module(gen)
except Exception as e:
    print(f"CANNOT CHECK: cannot import the generator: {type(e).__name__}: {e}")
    sys.exit(2)

store = {r["f"]: r for r in json.load(open("corpus/store_clean.json"))}
recs = [r for r in gen.recs if r["f"] in store][:60]
if not recs:
    print("CANNOT CHECK: no generator record is in the store")
    sys.exit(2)

# Ask the SHIPPED assembler for each one, in a single batch.
lines = [f"{store[r['f']]['rid']}\tA question.\t" for r in recs]
p = subprocess.run([ASM, STORE], input="\n".join(lines) + "\n", capture_output=True, text=True)
prompts = [l for l in p.stdout.split("\n") if l]
if len(prompts) != len(recs):
    print(f"CANNOT CHECK: asmcli returned {len(prompts)} prompts for {len(recs)} records")
    sys.exit(2)

def unit_vars(span):
    """Read the units field by splitting from the RIGHT.

    The first version of this split from the left and took field [1] -- which on `f_beat=|f_2-f_1|`
    lands inside the FORMULA, and the gate duly reported a mismatch in the shipped assembler. The
    assembler was correct; the oracle was not. Third time in this repo that an assertion's failure
    was the oracle's, and this one also pointed at the same bug living for real in
    tlm_shape_check_doc, which read the formula up to the first '|'. So: check the oracle before the
    subject, and then check whether the subject has the oracle's bug too."""
    parts = span.rsplit("|", 4)        # units | missing: | condition | fit:
    field = parts[1] if len(parts) == 5 else ""
    return {m.split(":")[0] for m in field.split() if ":" in m}

def five(span):
    """The five fields, split from the RIGHT for the reason unit_vars documents."""
    parts = span.rsplit("|", 4)
    return parts if len(parts) == 5 else None


# ALL FIVE FIELDS, not just units. Comparing one field was itself the defect: while this gate
# checked units and reported clean, the CONDITION field differed on 81 of 141 records (57.4%) --
# the device applies assemble.c:94, `req ? req : "standard conditions"`, and the generator applied
# a name-derived lookup. A parity gate that checks one field of five asserts parity and measures
# a fifth of it.
bad = []
for r, prompt in zip(recs, prompts):
    if prompt.startswith("!"): continue
    span = prompt.split("<r>", 1)[1]
    dev = five(span)
    if dev is None:
        bad.append((r["f"], "field count", span[:40], "not 5 fields")); continue
    # CALL the generator's own functions. The first version re-implemented units_field here, so
    # mutating corpus/generate.py did not move this gate and the negative control passed with the
    # fix reverted -- a gate carrying its own copy of the rule tests the copy.
    if unit_vars(span) != {m.split(":")[0] for m in gen.units_field(r).split() if ":" in m}:
        bad.append((r["f"], "units", dev[1].strip()[:38], gen.units_field(r)[:38]))
    if dev[3].strip() != gen._condition_field(r).strip():
        bad.append((r["f"], "condition", dev[3].strip()[:38], gen._condition_field(r)[:38]))
    if not dev[4].strip().startswith("fit:"):
        bad.append((r["f"], "fit", dev[4].strip()[:38], "expected fit:*"))
    if not dev[2].strip().startswith("missing:"):
        bad.append((r["f"], "missing", dev[2].strip()[:38], "expected missing:*"))

print(f"compared {len(recs)} records x 4 fields: generator record span vs the shipped assembler")
if bad:
    print(f"{len(bad)} MISMATCH(ES) -- the model would never see what the device emits:")
    for f, field, d, g in bad[:15]:
        print(f"  {f:26} {field:10} device {d!r}   generator {g!r}")
    sys.exit(1)
print("clean")
