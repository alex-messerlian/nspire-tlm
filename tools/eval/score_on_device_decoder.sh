#!/usr/bin/env bash
# Score checkpoints on the CALCULATOR'S DECODER: int8 weights, greedy, the app's tool loop, prompts
# built by the device's own ask_build + ns_assemble. Reproduces every quality number in the paper.
#
#   tools/eval/score_on_device_decoder.sh <group> <checkpoint.pt>...
#
# The engine's own group (FIXED_GS in src/runq_nspire.c: 88, with the hidden width padded to 1,056)
# is what ships; group 16 divides every row length at widths 176 and 352 and scores the width
# ladder. Any other group is refused. Exports go to build/int8/ (gitignored); results to
# results/{correct,arms}_int8_<TAG>_<name>.json, where TAG defaults to g<group>.
set -euo pipefail
G="${1:?usage: score_on_device_decoder.sh <group> <checkpoint.pt>...}"; shift
# Results are named results/{correct,arms}_int8_<TAG>_<name>.json. TAG defaults to g<group>, but
# pass TAG explicitly when the LAYOUT differs at the same group: a padded group-88 run once
# overwrote the committed results of the defective, unpadded group-88 engine under the same name.
TAG="${TAG:-g$G}"
PY=.venv-tok/bin/python
make -s build/devasm build/int8gen build/int8gen_g16
# build/int8gen is compiled at the ENGINE's group (FIXED_GS in src/runq_nspire.c, what ships);
# build/int8gen_g16 at CONTROL_GS. Any other group has no decoder, and is refused rather than run on
# a binary whose compiled group does not match the file.
SHIP_GS=$(grep -oE '^#define FIXED_GS [0-9]+' src/runq_nspire.c | grep -oE '[0-9]+$')
if [ "$G" = "$SHIP_GS" ]; then BIN=build/int8gen
elif [ "$G" = 16 ]; then BIN=build/int8gen_g16
else echo "no decoder built for group $G (engine is $SHIP_GS, control is 16)"; exit 2; fi
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
  Q80_PAD_TO=$G Q80_GROUP=$G $PY tools/legacy_to_q80.py "build/int8/${NAME}_g${G}_legacy.bin" "$OUT" | tail -3
  rm -f "build/int8/${NAME}_g${G}_legacy.bin"
  echo "== $NAME  group $G"
  INT8GEN=$BIN $PY tools/eval/score_correct.py --int8 "$OUT" "$CK" "results/correct_int8_${TAG}_${NAME}.json" | grep -v control
  INT8GEN=$BIN $PY tools/eval/score_int8_arms.py "$OUT" "$CK" "results/arms_int8_${TAG}_${NAME}.json" | grep -v control
done
