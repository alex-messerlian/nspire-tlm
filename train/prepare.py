#!/usr/bin/env python3
"""Tokenize the real 85/15 mix into a flat uint16 stream for training."""
import json, re, pathlib, random, numpy as np, sys
from tokenizers import Tokenizer, models, trainers, pre_tokenizers, decoders
V = int(sys.argv[1]) if len(sys.argv) > 1 else 4096
SPECIAL = ["<q>","</q>","<r>","<a>","<tool>","<arg>","</tool>","<res>","</res>","<end>"]
TAG,WS,MATH = re.compile(r"<[^>]+>"),re.compile(r"\s+"),re.compile(r"<m:math.*?</m:math>",re.S)
def clean(p): return WS.sub(" ", TAG.sub(" ", MATH.sub(" [MATH] ", p.read_text(errors="ignore")))).strip()

syn = [json.loads(l)["text"] for l in open("corpus/synth_sample.jsonl")]
# ALL TWELVE BOOKS, not the three physics ones.
#
# corpus/raw holds 12 OpenStax books, all downloaded. This read three of them -- 2,825,221 tokens --
# while astronomy, chemistry, calculus, algebra, statistics, precalculus, prealgebra and
# contemporary mathematics sat unused: 7,806,218 further tokens already on disk. Measured totals:
#
#   3 physics books    2,825,221 tokens   ->  11.3M usable at the <=4-epoch ceiling
#   all 12 books      10,631,439 tokens   ->  42.5M usable
#
# That was the constraint reported as binding the corpus size, and it was a reading of the loader
# rather than of the disk. It also matches the charter's plan of continued training per domain --
# algebra, calculus, physics, statistics are all here.
OER_BOOKS = sorted(d.name for d in pathlib.Path("corpus/raw").iterdir()
                   if d.is_dir() and any(d.rglob("*.cnxml")))
oer = [clean(f) for r in OER_BOOKS for f in (pathlib.Path("corpus/raw")/r).rglob("*.cnxml")]
rng = random.Random(20260820); rng.shuffle(syn); rng.shuffle(oer)
print(f"synthetic {len(syn):,} docs   OER {len(oer):,} modules from {len(OER_BOOKS)} books")

tk = Tokenizer(models.BPE(unk_token="<unk>"))
tk.pre_tokenizer = pre_tokenizers.ByteLevel(add_prefix_space=True)
# Without a matching decoder, decode() returns ByteLevel artifacts ("Ġ v = r * omega") rather than
# text. Training is unaffected -- ids are ids -- but every downstream inspection is garbled, and the
# device port needs this to print anything a human can read.
tk.decoder = decoders.ByteLevel()
tk.train_from_iterator(syn + oer, trainers.BpeTrainer(
    vocab_size=V, special_tokens=["<unk>"]+SPECIAL, show_progress=False))
tk.save(f"train/tok{V}.json")
ids = {t: tk.token_to_id(t) for t in SPECIAL}
assert all(v is not None and v < 11 for v in ids.values()), ids
print(f"tokenizer vocab {V}, specials at ids {sorted(ids.values())}")

# INTERLEAVE. Writing all synthetic then all OER makes any tail-slice validation set pure OER,
# so val loss measures out-of-distribution prose rather than held-out mix. Found in L0: train 1.71
# against val 5.03, which read as overfitting and was a distribution mismatch.
syn_ids = [tk.encode(d).ids for d in syn]
oer_ids = [tk.encode(d).ids for d in oer]
syn_tok = sum(len(x) for x in syn_ids)
budget  = int(syn_tok * 0.15 / 0.85)
pool, used = [], 0
for x in oer_ids:
    if used >= budget: break
    pool.append(x); used += len(x)
docs = syn_ids + pool
rng.shuffle(docs)                                  # mix before splitting
stream = [t for d in docs for t in d]
a = np.array(stream, dtype=np.uint16)
cut = int(len(a)*0.99)
a[:cut].tofile(f"train/mix{V}_train.bin")
a[cut:].tofile(f"train/mix{V}_val.bin")
print(f"wrote {len(a):,} tokens: {cut:,} train / {len(a)-cut:,} val")
print(f"  synthetic {syn_tok:,} tok   OER {used:,} tok   = {100*used/(syn_tok+used):.0f}% OER")
