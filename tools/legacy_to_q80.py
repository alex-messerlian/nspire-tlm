#!/usr/bin/env python3
"""Convert a llama2.c LEGACY fp32 checkpoint to the VERSION 2 (Q8_0 int8) format runq.c reads.

Why this exists: llama2.c's own export.py needs PyTorch, and the host has no torch and no numpy.
This does the same job with the stdlib only. It also means the quantiser is ours, which matters --
the same grouping and rounding rules have to be reimplemented on-device eventually.

Legacy layout: 7 int32 header, then fp32 tensors in a fixed order (see llama2.c export.py).
V2 layout:     256-byte header, fp32 norms, then per-tensor int8 values followed by fp32 scales.
"""
import os, struct, sys, array, pathlib

# Q80_GROUP overrides the ceiling -- used by the row-alignment control (docs/RESULT_CORRECTNESS.md),
# which needs a group that divides every ROW length, not only every tensor length.
GROUP = int(os.environ.get("Q80_GROUP", 96))

def read_f32(f, n):
    a = array.array('f')
    a.fromfile(f, n)
    if sys.byteorder != 'little':
        a.byteswap()
    return a

def quantize_q80(w, group_size=GROUP):
    """Symmetric int8 into [-127,127], per group of `group_size`. Mirrors export.py exactly."""
    assert len(w) % group_size == 0, f"{len(w)} not divisible by {group_size}"
    q = array.array('b', bytes(len(w)))
    scales = array.array('f', bytes(4 * (len(w) // group_size)))
    maxerr = 0.0
    for gi in range(len(w) // group_size):
        base = gi * group_size
        grp = w[base:base + group_size]
        wmax = max(abs(x) for x in grp)
        scale = wmax / 127.0
        scales[gi] = scale
        if scale == 0.0:
            for j in range(group_size):
                q[base + j] = 0
            continue
        inv = 1.0 / scale
        for j in range(group_size):
            v = grp[j] * inv
            iv = int(v + 0.5) if v >= 0 else -int(-v + 0.5)   # round-half-away-from-zero
            if iv > 127: iv = 127
            if iv < -127: iv = -127
            q[base + j] = iv
            err = abs(iv * scale - grp[j])
            if err > maxerr: maxerr = err
    return q, scales, maxerr

def main(src, dst):
    f = open(src, 'rb')
    dim, hidden_dim, n_layers, n_heads, n_kv_heads, vocab_size, max_seq_len = \
        struct.unpack('iiiiiii', f.read(28))
    shared = vocab_size > 0
    vocab_size = abs(vocab_size)
    head_size = dim // n_heads
    print(f"dim={dim} hidden={hidden_dim} layers={n_layers} heads={n_heads} "
          f"kv_heads={n_kv_heads} vocab={vocab_size} seq={max_seq_len} shared_classifier={shared}")

    # ---- read legacy tensors, in legacy order ----
    tok_emb   = read_f32(f, vocab_size * dim)
    att_norm  = [read_f32(f, dim) for _ in range(n_layers)]
    wq        = [read_f32(f, dim * n_heads    * head_size) for _ in range(n_layers)]
    wk        = [read_f32(f, dim * n_kv_heads * head_size) for _ in range(n_layers)]
    wv        = [read_f32(f, dim * n_kv_heads * head_size) for _ in range(n_layers)]
    wo        = [read_f32(f, n_heads * head_size * dim)    for _ in range(n_layers)]
    ffn_norm  = [read_f32(f, dim) for _ in range(n_layers)]
    w1        = [read_f32(f, dim * hidden_dim) for _ in range(n_layers)]
    w2        = [read_f32(f, hidden_dim * dim) for _ in range(n_layers)]
    w3        = [read_f32(f, dim * hidden_dim) for _ in range(n_layers)]
    final_norm = read_f32(f, dim)
    read_f32(f, max_seq_len * head_size // 2)   # freqs_cos, unused by runq.c (recomputed)
    read_f32(f, max_seq_len * head_size // 2)   # freqs_sin
    out_w = None if shared else read_f32(f, vocab_size * dim)
    leftover = f.read()
    f.close()
    if leftover:
        print(f"WARNING: {len(leftover)} trailing bytes unread -- layout assumption may be wrong")

    # ---- A153: pad the hidden width to a multiple of the engine's group, exactly ----
    # The engine groups each row from its start, so every row length must be a multiple of the
    # group (A151). Rows are `dim` wide for every matrix except w2, whose rows are `hidden_dim`
    # wide. Shrinking the group to one that divides both (32 at dim 352 / hidden 1024) is correct
    # but costs 27% per token on this core, measured: each group adds soft-float scale work. So
    # instead the hidden width is padded to a multiple of the group with ZERO weights:
    #   w1, w3 (hidden rows x dim): new rows are zero, so the new hidden units are silu(0)*0 = 0;
    #   w2     (dim rows x hidden): each row gains zero columns, which multiply those zeros.
    # The function computed is unchanged. Q80_PAD_TO names the group; unset, nothing is padded.
    pad_to = int(os.environ.get("Q80_PAD_TO", "0"))
    if pad_to and hidden_dim % pad_to:
        hp = -(-hidden_dim // pad_to) * pad_to
        extra = hp - hidden_dim
        z = array.array('f', bytes(4 * dim * extra))
        w1 = [t + z for t in w1]
        w3 = [t + z for t in w3]
        w2p = []
        for t in w2:
            r = array.array('f')
            zr = array.array('f', bytes(4 * extra))
            for row in range(dim):
                r.extend(t[row * hidden_dim:(row + 1) * hidden_dim]); r.extend(zr)
            w2p.append(r)
        w2 = w2p
        print(f"PADDED hidden {hidden_dim} -> {hp} with zeros (a multiple of {pad_to}); "
              f"+{3 * n_layers * dim * extra:,} stored parameters, the same function")
        hidden_dim = hp

    # ---- write v2 ----
    # GROUP SELECTION. THE OLD LOOP HALVED UNTIL IT DIVIDED, AND SILENTLY BOTTOMED OUT AT 1.
    #
    # It checked only `dim % group` and halved on failure: for dim=352 that is
    # 96 -> 48 -> 24 -> 12 -> 6 -> 3 -> 1, and group 1 means ONE FP32 SCALE PER INT8 VALUE.
    # The output is 5 bytes per parameter -- larger than the fp32 input -- and quantisation error is
    # exactly 0.000000, which reads like a flawless conversion. A d352 checkpoint came out at
    # 52.01 MiB against a 10.43 MiB prediction and 241% of the device's single-malloc ceiling.
    #
    # Two things were wrong. It checked ONE dimension rather than every tensor's length, and it
    # DEGRADED instead of failing. Now: take the largest divisor of every tensor length that is
    # <= GROUP, and abort below a floor where the scales stop being a rounding cost and become the
    # payload.
    _lens = {len(t) for t in [tok_emb] + wq + wk + wv + wo + w1 + w2 + w3}
    if not shared:
        _lens.add(len(out_w))
    # AND EVERY ROW LENGTH (A151). The engine groups each ROW from its start and ignores any
    # remainder, so a group must divide dim and hidden_dim, not only each tensor's total length --
    # 88 divided every tensor of the d352 model and not its 1,024-wide rows. rq_probe now refuses
    # such a file; choosing the group here from the right property means it is never produced.
    _rows = {dim, hidden_dim}
    group = max((g for g in range(1, GROUP + 1)
                 if all(L % g == 0 for L in _lens) and all(r % g == 0 for r in _rows)), default=1)
    GROUP_FLOOR = min(32, GROUP)
    if group < GROUP_FLOOR:
        raise SystemExit(
            f"ABORT: no group size <= {GROUP} divides every tensor length {sorted(_lens)}; the "
            f"largest is {group}, below the floor of {GROUP_FLOOR}. At group {group} a scale costs "
            f"{4/group:.2f} bytes per parameter and the 'quantised' file would be larger than the "
            f"fp32 input. Choose a dim whose tensor lengths share a larger factor.")
    if group != GROUP:
        print(f"GROUP {group} (not {GROUP}): {GROUP} does not divide every tensor length. "
              f"{1 + 4/group:.4f} bytes/param against {1 + 4/GROUP:.4f} -- "
              f"{100*((1+4/group)/(1+4/GROUP)-1):+.2f}%")

    # THE ENGINE GROUPS PER ROW, NOT PER TENSOR. runq_nspire.c's quantize() and matmul() walk each
    # row in steps of GS from the row's start and ignore any remainder, so a group that divides every
    # tensor LENGTH (the test above) but not every ROW length silently drops the tail of each row and
    # applies the neighbouring group's scale to part of each chunk. Row lengths are `dim` (every
    # matrix but w2) and `hidden_dim` (w2). At dim 352, hidden 1024, group 88 this drops 56 of 1024
    # hidden units in every layer, and it cost the shipped model 9.2 points of strict answer
    # accuracy (docs/RESULT_CORRECTNESS.md). Reported here, loudly, until the engine is fixed.
    bad_rows = sorted({n for n in (dim, hidden_dim) if n % group})
    if bad_rows:     # unreachable by construction above; kept so a future edit cannot reintroduce it
        raise SystemExit(f"ABORT: group {group} does not divide row length(s) {bad_rows}; the engine "
                         "would ignore the remainder of every row (A151, docs/RESULT_CORRECTNESS.md)")

    o = open(dst, 'wb')
    o.write(struct.pack('I', 0x616b3432))               # magic "ak42"
    o.write(struct.pack('i', 2))                        # version
    o.write(struct.pack('iiiiiii', dim, hidden_dim, n_layers, n_heads,
                        n_kv_heads, vocab_size, max_seq_len))
    o.write(struct.pack('B', int(shared)))
    o.write(struct.pack('i', group))
    pad = 256 - o.tell()
    assert pad >= 0, "header overflowed 256 bytes"
    o.write(b'\0' * pad)

    for t in att_norm:  t.tofile(o)
    for t in ffn_norm:  t.tofile(o)
    final_norm.tofile(o)

    to_quant = [tok_emb] + wq + wk + wv + wo + w1 + w2 + w3
    if not shared:
        to_quant.append(out_w)

    worst = 0.0
    for i, t in enumerate(to_quant):
        q, s, err = quantize_q80(t, group)
        q.tofile(o)
        s.tofile(o)
        worst = max(worst, err)
        print(f"  [{i+1}/{len(to_quant)}] {len(t):>9} vals  maxerr={err:.6f}", flush=True)
    o.close()
    print(f"wrote {dst}  ({pathlib.Path(dst).stat().st_size} bytes)  worst quant err={worst:.6f}")

if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
