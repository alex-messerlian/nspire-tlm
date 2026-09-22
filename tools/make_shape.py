#!/usr/bin/env python3
"""Write a VERSION 2 (int8 Q8_0) checkpoint of a given SHAPE with random weights.

WHY RANDOM WEIGHTS ARE LEGITIMATE HERE, and where the claim stops.

Decode throughput on this engine is a function of GEOMETRY, not of weight values: every matmul
runs a fixed trip count, the int8 kernel has no data-dependent branch, and nothing is sparse or
early-exited. So the throughput axis of the parameters-vs-throughput frontier can be measured
without training anything, which is the difference between a frontier and a single point.

The claim stops at throughput. These files say NOTHING about quality -- no perplexity, no
correctness, and they must never be scored. That assumption is not asserted by argument either:
tools/make_shape.py --seed is used to write two files of the SAME shape with DIFFERENT weights,
and bench_sweep times both. If weights mattered the two would differ.

Written directly in V2 rather than via legacy fp32 + tools/legacy_to_q80.py, because that path
quantises in pure Python over millions of values and the values here are arbitrary anyway.
"""
import argparse, math, random, struct
from pathlib import Path

MAGIC = 0x616b3432
GROUP_MAX, GROUP_FLOOR = 96, 32


def hidden_for(dim: int) -> int:
    """llama2's convention: 8/3 * dim, rounded up to a multiple of 256. d352 -> 1024, as shipped."""
    return math.ceil((8 * dim / 3) / 256) * 256


def pick_group(lengths) -> int:
    """Largest g <= GROUP_MAX dividing every tensor length.

    Same rule as tools/legacy_to_q80.py, including its floor. That tool once halved until it
    divided and bottomed out at g=1 -- one fp32 scale per int8 value, 5 bytes per parameter, and
    a quantisation error of exactly 0.000000 that read as a flawless conversion. Below the floor
    this aborts instead of degrading.
    """
    g = max((g for g in range(1, GROUP_MAX + 1) if all(L % g == 0 for L in lengths)), default=1)
    if g < GROUP_FLOOR:
        raise SystemExit(f"ABORT: largest common group is {g}, below the floor of {GROUP_FLOOR}. "
                         f"At g={g} a scale costs {4/g:.2f} bytes per parameter.")
    return g


def build(dim, layers, heads, vocab, seq, seed, out: Path):
    hidden = hidden_for(dim)
    if dim % heads:
        raise SystemExit(f"dim {dim} is not divisible by {heads} heads")
    rng = random.Random(seed)

    # Tensor lengths in the order runq.c reads them. Classifier is SHARED with the embedding,
    # matching the shipped checkpoint.
    quant_lens = ([vocab * dim]
                  + [dim * dim] * (4 * layers)          # wq wk wv wo
                  + [dim * hidden] * (3 * layers))      # w1 w2 w3
    group = pick_group(set(quant_lens))

    with out.open("wb") as o:
        o.write(struct.pack("I", MAGIC))
        o.write(struct.pack("i", 2))
        o.write(struct.pack("iiiiiii", dim, hidden, layers, heads, heads, vocab, seq))
        o.write(struct.pack("B", 1))          # shared classifier
        o.write(struct.pack("i", group))
        o.write(b"\0" * (256 - o.tell()))

        # fp32 norms: att_norm per layer, ffn_norm per layer, final_norm. Ones, so rmsnorm is a
        # no-op scale rather than a source of infinities -- the timing must not depend on hitting
        # a denormal path.
        ones = struct.pack("f", 1.0)
        o.write(ones * (dim * (2 * layers + 1)))

        # int8 payload then fp32 scales, per tensor.
        #
        # Values are small and scales are 1/sqrt(dim), so activations stay in a normal range
        # through six layers. Garbage that overflows to inf would push expf and the softmax onto
        # a different path and the timing would no longer be the timing of a real model.
        scale = 1.0 / math.sqrt(dim)
        for n in quant_lens:
            o.write(bytes(rng.randrange(-8, 9) & 0xFF for _ in range(n)))
            o.write(struct.pack("f", scale) * (n // group))

    params = sum(quant_lens)
    print(f"{out.name}: dim={dim} hidden={hidden} L={layers} group={group} "
          f"params={params:,} bytes={out.stat().st_size:,} "
          f"({out.stat().st_size / params:.4f} B/param)")
    return params, out.stat().st_size


if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("--dim", type=int, required=True)
    p.add_argument("--layers", type=int, default=6)
    p.add_argument("--heads", type=int, default=8)
    p.add_argument("--vocab", type=int, default=4096)
    p.add_argument("--seq", type=int, default=512)
    p.add_argument("--seed", type=int, default=1)
    p.add_argument("--out", type=Path, required=True)
    a = p.parse_args()
    build(a.dim, a.layers, a.heads, a.vocab, a.seq, a.seed, a.out)
