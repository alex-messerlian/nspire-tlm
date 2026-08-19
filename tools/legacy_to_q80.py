#!/usr/bin/env python3
"""Convert a llama2.c LEGACY fp32 checkpoint to the VERSION 2 (Q8_0 int8) format runq.c reads.

Why this exists: llama2.c's own export.py needs PyTorch, and the host has no torch and no numpy.
This does the same job with the stdlib only. It also means the quantiser is ours, which matters --
the same grouping and rounding rules have to be reimplemented on-device eventually.

Legacy layout: 7 int32 header, then fp32 tensors in a fixed order (see llama2.c export.py).
V2 layout:     256-byte header, fp32 norms, then per-tensor int8 values followed by fp32 scales.
"""
import struct, sys, array, pathlib

GROUP = 64

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

    # ---- write v2 ----
    group = GROUP
    while dim % group != 0:
        group //= 2
        print(f"BACKOFF: group size -> {group}")

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
