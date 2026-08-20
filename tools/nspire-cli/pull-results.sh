#!/bin/sh
# pull-results.sh -- retrieve every log after a device session. Run with USB reconnected.
#
# Missing files are reported, not fatal: a partial session is normal and the point is to get
# whatever was produced.
set -u
cd "$(dirname "$0")/../.."
NSP=tools/nspire-cli/nsp
OUT=results/$(date +%Y%m%d-%H%M%S)
mkdir -p "$OUT"

for f in eval_device.txt results.txt llama2_device.txt; do
    if $NSP pull "/documents/bench/${f}.tns" "$OUT/$f" 2>/dev/null; then :; else
        echo "  MISSING: $f  (that program may not have run)"
    fi
done

echo
echo "pulled into $OUT:"
ls -la "$OUT" | tail -n +2
echo
echo "--- the two gating numbers ---"
grep -h "RESULT=" "$OUT/eval_device.txt" 2>/dev/null || echo "  eval_device verdict: NOT FOUND"
grep -h "B1_largest_malloc_bytes" "$OUT/results.txt" 2>/dev/null || echo "  B1 heap: NOT FOUND"
grep -h "tok_per_s\|cpu_hz\|power=" "$OUT/llama2_device.txt" 2>/dev/null || true
