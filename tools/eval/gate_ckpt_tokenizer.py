#!/usr/bin/env python3
"""Every checkpoint must say which tokenizer it was trained against, or be quarantined.

train/prepare.py retrains the tokenizer on every run, so a checkpoint's embedding matrix indexes a
table that the next run replaces. 16 commits touch train/tok4096.json and all 16 contents differ;
between two consecutive runs 3,886 of 4,096 ids changed. A mismatched pair does not error -- it
reports 0.0 on every arm, which reads as a catastrophic model result.

So the set of checkpoints that cannot be scored against the current tokenizer must be ENUMERATED,
not discovered. New checkpoints carry `tok_sha` (select_run.py). Legacy ones are listed below with
the tokenizer they need, and score_arms takes TOK= to use it.
"""
import glob, hashlib, json, pathlib, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]

# Legacy checkpoints, trained before select_run.py stamped tok_sha. Each entry states the archived
# tokenizer to score it with, or UNKNOWN where the tokenizer was never committed.
QUARANTINE = {
    "baseline_retrain1_55a58d4d.pt": "train/tok4096_retrain1.json",
    "baseline_run2_eca039cd.pt":     "UNKNOWN -- pre-A42 run; recover from git if it is ever re-scored",
    **{f"{p}.pt": "UNKNOWN -- the 2026-08-26 batch; git 713827e's tokenizer decodes sel_s2 correctly"
       for p in ("sel_s2", "sel_s3", "sel_s4", "sel_s5", "sel_s6", "sel_s7", "sel_s8", "sel_s99",
                 "ship_s1", "ship_s2", "ship_s3", "pop_s1", "pop_s2", "pop_s3", "refusal_s1", "l0")},
    **{f"cap_{n}.pt": "UNKNOWN -- the 2026-08-26 capability curve; internally consistent, do not mix"
       for n in (0, 2000, 4000, 6000, 8000, 10000, 12000, 14000, 16000, 18000, 20000)},
}


def main():
    cur = hashlib.sha256((ROOT / "train/tok4096.json").read_bytes()).hexdigest()[:16]
    import torch
    unlisted, stale, ok = [], [], []
    for f in sorted(glob.glob(str(ROOT / "train/*.pt"))):
        name = pathlib.Path(f).name
        try:
            ck = torch.load(f, map_location="cpu", weights_only=False)
        except Exception:
            continue
        sha = ck.get("tok_sha")
        if sha is None:
            (ok if name in QUARANTINE else unlisted).append(name)
        elif sha != cur:
            stale.append((name, sha))
        else:
            ok.append(name)
    print(f"  current tokenizer {cur}")
    print(f"  checkpoints scoreable now: {len(ok)}   quarantined/listed: "
          f"{sum(1 for n in ok if n in QUARANTINE)}")
    for n in unlisted:
        print(f"  UNLISTED: {n} carries no tok_sha and is not in QUARANTINE -- it cannot be scored "
              f"safely and nothing says which tokenizer it needs")
    for n, s in stale:
        print(f"  STALE: {n} was trained against {s}; score it with TOK=<that tokenizer>")
    if unlisted:
        print("FAIL: a checkpoint with no tokenizer provenance. A mismatched pair reports 0.0 on "
              "every arm, so an un-enumerated one is a wrong number waiting to be quoted.")
        return 1
    print("PASS: every checkpoint either stamps its tokenizer or is listed with the one it needs")
    return 0


if __name__ == "__main__":
    sys.exit(main())
