#!/usr/bin/env python3
"""F1 follow-ups: the corpus shape and the device shape are the same shape.

WHY. `app_context()` built a conversation history and nothing called it, so every turn on the
calculator was turn one -- and a second turn's prompt occurred in 0 of 239,853 training documents.
docs/RESULT_FOLLOWUP_UNWIRED.md says the two halves ship together, because wiring the device alone
emits a prompt shape the corpus does not have, which is the RESULT_CANNOT_EXPLAIN failure exactly.

This gate is what makes "together" checkable rather than a claim in a commit message:

  1. THE PREFIX IS THE DEVICE'S LITERAL. app.c writes "Earlier: %.90s " and the corpus must use the
     same bytes. A cosmetic edit to one and not the other is a train/serve skew in the first eight
     characters of every follow-up prompt, and this repo has found that class five times in other
     fields (units, condition, fit, missing, the result span).
  2. THE CONTEXT REACHES RETRIEVAL, NOT JUST THE MODEL. A follow-up names no relation -- "recompute",
     "what units is that in?" -- so it must be prepended BEFORE ask_build, and the measurement that
     makes this concrete is that the follow-up alone retrieves a different record than the pair does.
  3. F1 DOCUMENTS EXIST. Zero is the state this gate was written to end, and "cannot check" does not
     share an exit status with "checked and clean".
"""
import pathlib, re, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "corpus"))
CORPUS = ROOT / "corpus/synth_sample.jsonl"
STORE = ROOT / "build/store.tns"
DEVP = ROOT / "build/devprompt"


def device_prefix():
    """The literal app_context emits, read from the source rather than duplicated here."""
    src = (ROOT / "src/store/app.c").read_text()
    m = re.findall(r'snprintf\(one, sizeof one, "([^"%]*)%', src)
    return m[0] if m else None


def prompt_of(text):
    r = subprocess.run([str(DEVP), str(STORE), text, "0"], capture_output=True, text=True)
    return next((l for l in r.stdout.split("\n") if l.startswith("<q>")), None)


def record_of(pre):
    import recfmt
    return recfmt.formula(pre.split("<r>", 1)[1]) if pre and "<r>" in pre else None


def main():
    pre = device_prefix()
    if not pre:
        print("  CANNOT CHECK: no 'Earlier: ' literal found in app.c. Not a pass.")
        return 2
    print(f"  device prefix, read from app.c: {pre!r}")

    if not DEVP.exists() or not STORE.exists():
        print("  CANNOT CHECK: build/devprompt or build/store.tns missing. Not a pass.")
        return 2

    # (2) THE MEASUREMENT, not an assertion about the code: the follow-up alone and the pair must
    # retrieve DIFFERENT records, which is the whole reason the context goes in before ask_build.
    q1, q2 = "calculate kinetic energy. m = 2, v = 3", "recompute. v = 5"
    alone, pair = prompt_of(q2), prompt_of(f"{pre}{q1} {q2}")
    ra, rp = record_of(alone), record_of(pair)
    print(f"  follow-up alone retrieves : {ra}")
    print(f"  with the earlier turn     : {rp}")
    if rp != "K=0.5*m*(v)^(2)":
        print(f"  FAIL: the pair retrieves {rp!r}, not the relation the earlier turn names. The "
              f"context is not reaching retrieval.")
        return 1
    if ra == rp:
        print("  CONTROL BROKEN: the follow-up alone already retrieves the right record, so this "
              "probe cannot show that the context is doing the work. Pick a blinder follow-up.")
        return 2
    print("  control: the follow-up alone retrieves something else, so the context is load-bearing")

    # A113's last-wins, in the shape a follow-up always has.
    if "v = 5" not in pair or "v = 3" in pair:
        print(f"  FAIL: the restated value did not override. Prompt: {pair[:120]}")
        return 1
    print("  the restated value overrides (A113 last-wins)")

    # (3) and (1) over the shipped corpus.
    if not CORPUS.exists():
        print("  CANNOT CHECK: corpus absent. Not a pass.")
        return 2
    import json
    n = bad = 0
    for line in CORPUS.open():
        o = json.loads(line)
        if o.get("kind") != "F1":
            continue
        n += 1
        q = o["text"].split("<q>", 1)[1].split("</q>", 1)[0]
        if not q.startswith(pre):
            bad += 1
    print(f"  F1 documents in the corpus: {n:,}")
    if n == 0:
        print("\n  FAIL: zero F1 documents. The device now prepends the earlier turns, so a prompt "
              "shape with no training documents is exactly what RESULT_CANNOT_EXPLAIN describes. "
              "Regenerate with FOLLOWUP=1.")
        return 1
    if bad:
        print(f"\n  FAIL: {bad} F1 questions do not start with the device's own prefix {pre!r}.")
        return 1
    print(f"  all {n:,} start with the device's prefix")
    print("\n  PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
