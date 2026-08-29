#!/usr/bin/env python3
"""Regenerate build/tok_reference.json against the CURRENT tokenizer.

THE FIXTURE HAD NO GENERATOR, and that is why it went stale. It was written once on 2026-08-26 and
then retrain 2 replaced train/tok4096.json -- prepare.py retrained it on every run at the time -- so
577 of 581 cases mismatched and test_tokenizer failed for a reason that had nothing to do with the
device tokenizer it exists to check.

Third artefact in this class, after the checkpoints and build/store.tns: a derived file that goes
stale silently when its input moves. The durable fix is that prepare.py now REUSES an equivalent
tokenizer, so this should not need running again; it exists so that when it does, the fixture is
re-derived rather than hand-repaired.

The texts are kept verbatim from the existing fixture -- only the ids are re-derived, so the case
set does not drift while the reference does.
"""
import json, pathlib, sys
from tokenizers import Tokenizer

ROOT = pathlib.Path(__file__).resolve().parents[1]
ref = ROOT / "build/tok_reference.json"
old = json.loads(ref.read_text())
tk = Tokenizer.from_file(str(ROOT / "train/tok4096.json"))
new = [{"text": c["text"], "ids": tk.encode(c["text"]).ids} for c in old]
changed = sum(1 for a, b in zip(old, new) if a["ids"] != b["ids"])
ref.write_text(json.dumps(new))
print(f"  {len(new)} cases re-derived, {changed} ids changed "
      f"({100*changed/len(new):.1f}%) -> {ref.relative_to(ROOT)}")
