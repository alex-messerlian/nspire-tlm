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
import argparse
import re, math, random, struct
from pathlib import Path

MAGIC = 0x616b3432
GROUP_MAX, GROUP_FLOOR = 96, 32


def engine_group() -> int:
    """FIXED_GS as the engine source declares it -- one source of truth, never a typed default. A
    hardcoded 88 here would keep producing group-88 shapes after the engine moved (A151)."""
    src = (Path(__file__).resolve().parents[1] / "src/runq_nspire.c").read_text()
    m = re.search(r"^#define FIXED_GS (\d+)", src, re.M)
    if not m:
        raise SystemExit("ABORT: no '#define FIXED_GS' in src/runq_nspire.c")
    return int(m.group(1))


def hidden_for(dim: int) -> int:
    """llama2's convention: 8/3 * dim, rounded up to a multiple of 256. d352 -> 1024, as shipped."""
    return math.ceil((8 * dim / 3) / 256) * 256


def pick_group(lengths, force: int | None = None) -> int:
    """With `force`, demand exactly that group and fail loudly if it does not divide.

    THE ENGINE'S GROUP SIZE IS COMPILE-TIME. src/runq_nspire.c sets FIXED_GS 88 so the hot loop
    loses two __divsi3 per group, which matters on a core with no divider. A checkpoint built at
    any other group is refused at load with "GS n, built for 88" -- measured: a first sweep
    shipped d192/d256/d416 at groups 96/64/64 and three of five shapes were unloadable.

    So the shape ladder is not free: every tensor length AND every row length (dim and hidden, A151)
    must be a multiple of the group. At the row-aligned group 32, with eight heads, dim may be any
    multiple of 32 whose 8/3-rounded hidden width is also one: 192, 224, 256, ..., 448.
    """
    if force is not None:
        bad = sorted(L for L in lengths if L % force)
        if bad:
            raise SystemExit(
                f"ABORT: group {force} does not divide tensor lengths {bad}. The engine is built "
                f"for FIXED_GS {force} and would refuse this checkpoint at load.")
        return force
    return _largest_group(lengths)


def _largest_group(lengths) -> int:
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


def build(dim, layers, heads, vocab, seq, seed, out: Path, group_force=None):
    hidden = hidden_for(dim)
    if dim % heads:
        raise SystemExit(f"dim {dim} is not divisible by {heads} heads")
    rng = random.Random(seed)

    # Tensor lengths in the order runq.c reads them. Classifier is SHARED with the embedding,
    # matching the shipped checkpoint.
    quant_lens = ([vocab * dim]
                  + [dim * dim] * (4 * layers)          # wq wk wv wo
                  + [dim * hidden] * (3 * layers))      # w1 w2 w3
    # A151: the ROW lengths (dim, hidden) must be multiples of the group too -- the engine groups
    # each row from its start and ignores any remainder. rq_probe refuses a file that violates it.
    group = pick_group(set(quant_lens) | {dim, hidden}, group_force)

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
    p.add_argument("--group", type=int, default=engine_group(),
                   help="demand this group size; the default is FIXED_GS read from "
                        "src/runq_nspire.c, what the engine is built for. "
                        "Pass 0 to let the largest valid group be chosen instead.")
    p.add_argument("--out", type=Path, required=True)
    a = p.parse_args()
    build(a.dim, a.layers, a.heads, a.vocab, a.seq, a.seed, a.out, a.group or None)
