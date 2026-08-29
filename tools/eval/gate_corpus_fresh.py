#!/usr/bin/env python3
"""PERMANENT GATE: corpus/synth_sample.jsonl must have been produced by the CURRENT inputs.

WHY. A regeneration silently did not run and train/prepare.py read the previous corpus, producing
token counts identical to the run before -- 29,594,922 both times, after 26 relations had been
removed from the record set. Nothing failed. It was caught by noticing the counts were implausible,
which is not a check.

The corpus now carries a hash of every input that determines it (corpus/stamp.py), and this
recomputes that hash and compares. A stale corpus FAILS instead of reproducing.

ABSENCE IS A FAILURE, NOT A SKIP: a corpus with no stamp is one produced before stamping existed or
by something that bypassed generate.py, and either way it cannot be shown current.
"""
import pathlib, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "corpus"))
import stamp  # noqa: E402


def main():
    if not (ROOT / "corpus/synth_sample.jsonl").exists():
        print("  CANNOT CHECK: corpus/synth_sample.jsonl is missing.")
        return 2
    st = stamp.read()
    if st is None:
        print("  FAIL: the corpus carries no stamp, so it cannot be shown current.")
        print("  Regenerate with corpus/generate.py.")
        return 1
    now = stamp.input_hash()
    print(f"  stamped input hash : {st['input_hash']}")
    print(f"  current input hash : {now}")
    print(f"  documents          : {st.get('n_docs'):,}")
    if st["input_hash"] != now:
        print("\n  FAIL: the corpus was produced by DIFFERENT inputs than are on disk now. Any")
        print("  measurement taken from it describes a corpus that no longer exists. Regenerate.")
        return 1
    print("\n  PASS: the corpus matches the inputs that produced it")
    return 0


if __name__ == "__main__":
    sys.exit(main())
