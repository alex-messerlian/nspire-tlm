"""The corpus stamp: a hash of every input that determines what generate.py emits.

WHY. A regeneration silently did not run -- a backgrounded job died with its shell -- and
train/prepare.py then read the PREVIOUS corpus and produced token counts identical to the run
before it. Nothing failed. It was caught only because identical counts after removing 26 relations
is implausible, which is vigilance, not a check.

Same class as the checkpoint corpus_sha stamp, and the same fix: the artefact carries the identity
of what produced it, and a consumer compares rather than trusts. A stale read now FAILS instead of
reproducing.

INPUTS ARE ENUMERATED, NOT GLOBBED. A glob would silently start covering a new file, or silently
stop covering a renamed one, and either way the stamp would change meaning without anyone deciding.
"""
import hashlib, json, pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]

# Everything that determines the emitted corpus. Adding a new input to generate.py means adding it
# here; gate_corpus_fresh will not know about it otherwise.
INPUTS = [
    "corpus/generate.py",
    "corpus/store_clean.json",
    "corpus/units_train.json",
    "corpus/units_holdout.json",
    "corpus/d3_stems.json",
    "corpus/symbol_map.json",
    "corpus/empirical_values.json",
    "corpus/asks_dev.json",
    # MISSING UNTIL A64, and this file's own docstring predicted it: "Adding a new input to
    # generate.py means adding it here; gate_corpus_fresh will not know about it otherwise."
    # generate.py:1111 loads it into `_mined`, which supplies a record's NAME at :1452 and the
    # source record at :1484 -- so editing it changes the question surfaces and the stamp said
    # fresh. Found by auditing what generate.py opens against what this list enumerates.
    "corpus/records_raw.json",
]


def input_hash():
    h = hashlib.sha256()
    for rel in INPUTS:
        p = ROOT / rel
        h.update(rel.encode())
        h.update(b"\x00")
        h.update(p.read_bytes() if p.exists() else b"<absent>")
        h.update(b"\xff")
    return h.hexdigest()[:16]


# THE HASH IS TAKEN AT IMPORT, NOT AT WRITE.
#
# generate.py calls write() at the END of a run that takes twenty minutes. input_hash() reads
# corpus/generate.py and seven data files FROM DISK, so editing any of them mid-run stamped the
# corpus with the hash of code that did not produce it -- and gate_corpus_fresh would then PASS on
# a corpus built by the old generator. A FALSE GREEN, and the one this stamp exists to prevent.
#
# Same lesson as A50, which moved corpus_sha to the START of select_run.py for the same reason: a
# long-running producer must pin its inputs when it READS them, not when it finishes. I hit this
# while the regeneration was live and had to leave the generator alone for twenty minutes rather
# than trigger it.
_HASH_AT_IMPORT = input_hash()


def write(n_docs):
    (ROOT / "corpus/synth_stamp.json").write_text(json.dumps({
        "input_hash": _HASH_AT_IMPORT,
        "n_docs": n_docs,
        "inputs": INPUTS,
        "_doc": "Written by corpus/generate.py. tools/eval/gate_corpus_fresh.py recomputes this "
                "and fails when corpus/synth_sample.jsonl was produced by different inputs.",
    }, indent=1))


def read():
    p = ROOT / "corpus/synth_stamp.json"
    return json.loads(p.read_text()) if p.exists() else None
