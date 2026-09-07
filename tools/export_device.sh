#!/usr/bin/env bash
# Export a trained checkpoint to the device transfer set, in one step that cannot half-apply.
#
# THE STEPS THIS BUNDLES ARE THE ONES THAT WENT WRONG SEPARATELY:
#   - the quantiser silently degraded to group 1 when the group did not divide (d352, 52 MiB file
#     at error 0.000000). It now aborts below a floor; this passes the group explicitly anyway.
#   - the transfer set carried a tokenizer from a DIFFERENT run than the model. Repacked here from
#     the same tree, and the checkpoint's tok_sha is verified against it before anything is written.
#   - FIXED_GS lives in a header and in the Makefile; a model exported at one and a binary built at
#     another dies at read_checkpoint on device. Both are set from one argument.
set -euo pipefail
CK="${1:?usage: export_device.sh <checkpoint.pt> <group_size>}"
GS="${2:?usage: export_device.sh <checkpoint.pt> <group_size>}"
PY=.venv-tok/bin/python

echo "== verifying the checkpoint pairs with the tokenizer on disk =="
$PY - "$CK" <<'EOF'
import sys, hashlib, pathlib, torch
ck = torch.load(sys.argv[1], map_location="cpu", weights_only=False)
have = hashlib.sha256(pathlib.Path("train/tok4096.json").read_bytes()).hexdigest()[:16]
want = ck.get("tok_sha")
assert want, "checkpoint carries no tok_sha -- refusing to export a model whose tokenizer is unknown"
assert want == have, f"tokenizer mismatch: checkpoint {want}, on disk {have}"
a = ck["args"]
print(f"  paired: tok_sha={have}  dim={a['dim']} L={a['n_layers']} heads={a['n_heads']} seq={a['max_seq_len']}")
EOF

echo "== setting FIXED_GS=$GS in the engine and the host build =="
perl -pi -e "s/^#define FIXED_GS [0-9]+/#define FIXED_GS $GS/" src/runq_nspire.c
perl -pi -e "s/-DFIXED_GS=[0-9]+/-DFIXED_GS=$GS/g" Makefile
grep -m1 "define FIXED_GS" src/runq_nspire.c

echo "== exporting fp32 then quantising at group $GS =="
$PY - "$CK" <<'EOF'
import sys, torch, pathlib
sys.path.insert(0, "vendor/llama2.c")
from model import Transformer, ModelArgs
import export as ex
ck = torch.load(sys.argv[1], map_location="cpu", weights_only=False)
m = Transformer(ModelArgs(**ck["args"])); m.load_state_dict(ck["model"]); m.eval()
ex.legacy_export(m, "build/model_dev_legacy.bin")
EOF
$PY tools/legacy_to_q80.py build/model_dev_legacy.bin build/model_dev_gs.bin | tail -2
rm -f build/model_dev_legacy.bin

echo "== staging the transfer set =="
cp build/model_dev_gs.bin build/transfer/model4096.bin.tns
$PY tools/tok_pack.py >/dev/null && cp build/tok4096.tok build/transfer/tok4096.tok.tns
GS_HAVE=$(od -An -tu4 -j37 -N4 build/transfer/model4096.bin.tns | tr -d ' ')
[ "$GS_HAVE" = "$GS" ] || { echo "  FATAL: checkpoint says GS=$GS_HAVE, expected $GS"; exit 1; }
# THE DEVICE NEEDS THREE FILES AND THIS SCRIPT STAGED TWO. resolve_data_dir() in device_app.c
# requires store.tns.tns, tok4096.tok.tns AND model4096.bin.tns, and only the last two were
# refreshed here -- so a store correction reached build/store.tns and stopped, and the calculator
# kept running the old one. Found by comparing the staged copy against the build: 20,010 B against
# 20,275 B, different content, five days apart. The script's own header says "in one step that
# cannot half-apply"; it half-applied on the file that decides which relations exist at all.
#
# Same class as the store cleaning that reached store_clean.json and not build/store.tns, and as
# `need_file` checking existence rather than freshness. Rebuilt from source here rather than copied,
# so a stale build/store.tns cannot be laundered into the transfer set either.
# NOTE: store_pack.py IGNORES its second argument and always writes build/store.tns. The call below
# is correct only because the paths coincide; passing any other destination would silently write to
# build/store.tns anyway. Left as-is rather than changed under an export script, but recorded so the
# next reader does not believe the argument does something.
$PY tools/store_pack.py corpus/store_clean.json build/store.tns >/dev/null
cp build/store.tns build/transfer/store.tns.tns
echo "  model $(stat -f%z build/transfer/model4096.bin.tns) B at GS=$GS_HAVE, tokenizer repacked from the same tree"
echo "  store $(stat -f%z build/transfer/store.tns.tns) B, rebuilt from corpus/store_clean.json"
echo "  transfer set complete: $(ls build/transfer/*.tns | wc -l | tr -d ' ') files"
