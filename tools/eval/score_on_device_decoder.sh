#!/usr/bin/env bash
# Score checkpoints on the CALCULATOR'S DECODER: int8 weights, greedy, the app's tool loop, prompts
# built by the device's own ask_build + ns_assemble. Reproduces every quality number in the paper.
#
#   tools/eval/score_on_device_decoder.sh <group> <checkpoint.pt>...
#
# group 88 is what ships; group 16 divides every row length at widths 176 and 352 and is the
# control for the row-alignment defect (docs/RESULT_CORRECTNESS.md). Exports go to build/int8/
# (gitignored); results to results/{correct,arms}_int8_g<group>_<name>.json.
set -euo pipefail
G="${1:?usage: score_on_device_decoder.sh <group> <checkpoint.pt>...}"; shift
PY=.venv-tok/bin/python
make -s build/devasm build/int8gen build/int8gen_g16
BIN=build/int8gen; [ "$G" = 16 ] && BIN=build/int8gen_g16
[ "$G" = 88 ] || [ "$G" = 16 ] || { echo "no decoder built for group $G"; exit 2; }
mkdir -p build/int8
for CK in "$@"; do
  NAME=$(basename "$CK" .pt); OUT=build/int8/${NAME}_g${G}.bin
  $PY - "$CK" "build/int8/${NAME}_g${G}_legacy.bin" <<'PYEOF'
import sys, torch
sys.path.insert(0, "vendor/llama2.c")
import export as ex
from model import Transformer, ModelArgs
ck = torch.load(sys.argv[1], map_location="cpu", weights_only=False)
m = Transformer(ModelArgs(**ck["args"])); m.load_state_dict(ck["model"]); m.eval()
ex.legacy_export(m, sys.argv[2])
PYEOF
  Q80_GROUP=$G $PY tools/legacy_to_q80.py "build/int8/${NAME}_g${G}_legacy.bin" "$OUT" | tail -2
  rm -f "build/int8/${NAME}_g${G}_legacy.bin"
  echo "== $NAME  group $G"
  INT8GEN=$BIN $PY tools/eval/score_correct.py --int8 "$OUT" "$CK" "results/correct_int8_g${G}_${NAME}.json" | grep -v control
  INT8GEN=$BIN $PY tools/eval/score_int8_arms.py "$OUT" "$CK" "results/arms_int8_g${G}_${NAME}.json" | grep -v control
done
