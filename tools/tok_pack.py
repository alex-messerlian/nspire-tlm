"""Pack train/tok4096.json into a flat .tok file the device loads without a JSON parser.

KEY DECISION: the ByteLevel alphabet is decoded HERE, on the host. HuggingFace stores vocabulary
in GPT-2's byte->pseudo-character encoding (space is U+0120 'G-dot'), which would force the device
to carry a 256-entry unicode table and a UTF-8 decoder. Decoding at pack time means the C
tokenizer operates on RAW BYTES and never sees the alphabet at all.

Combined with the store being ASCII-only, the device tokenizer needs no unicode support whatever.

Format:
    NSTOK1
    <n_special> <n_vocab> <n_merges>
    S <id> <token>              (special tokens, matched before anything else, longest-first)
    V <id> <hex-of-raw-bytes>
    M <rank> <hex-a> <hex-b>
    END <n_vocab>
"""
import json, pathlib, sys

def byte_decoder():
    bs = list(range(33,127)) + list(range(161,173)) + list(range(174,256))
    cs = bs[:]; n = 0
    for b in range(256):
        if b not in bs: bs.append(b); cs.append(256+n); n += 1
    return {chr(c): b for b, c in zip(bs, cs)}

DEC = byte_decoder()
def to_bytes(tok):
    """GPT-2 pseudo-characters -> the raw bytes they stand for."""
    return bytes(DEC[c] for c in tok)

def pack(t):
    vocab = t["model"]["vocab"]
    merges = t["model"].get("merges", [])
    special = {a["content"]: a["id"] for a in t.get("added_tokens", [])}
    lines = ["NSTOK1", f"{len(special)} {len(vocab)} {len(merges)}"]
    # longest-first so <res> never shadows a longer token sharing its prefix
    for tok, i in sorted(special.items(), key=lambda kv: -len(kv[0])):
        assert "\t" not in tok and " " not in tok, f"special token with whitespace: {tok!r}"
        lines.append(f"S {i} {tok}")
    for tok, i in sorted(vocab.items(), key=lambda kv: kv[1]):
        if tok in special: continue
        lines.append(f"V {i} {to_bytes(tok).hex()}")
    for rank, m in enumerate(merges):
        a, b = m if isinstance(m, list) else m.split(" ", 1)
        lines.append(f"M {rank} {to_bytes(a).hex()} {to_bytes(b).hex()}")
    lines.append(f"END {len(vocab)}")
    return "\n".join(lines) + "\n"

if __name__ == "__main__":
    t = json.load(open("train/tok4096.json"))
    txt = pack(t)
    out = pathlib.Path("build/tok4096.tok"); out.parent.mkdir(exist_ok=True)
    tmp = out.with_suffix(".tmp"); tmp.write_text(txt); tmp.rename(out)
    print(f"packed vocab {len(t['model']['vocab'])}, merges {len(t['model'].get('merges',[]))}, "
          f"special {len(t.get('added_tokens',[]))} -> {out} ({len(txt)} bytes)")
