#!/bin/sh
# pull-results.sh -- retrieve every log after a device session. Run with USB reconnected.
#
# Missing files are reported, not fatal: a partial session is normal and the point is to get
# whatever was produced.
#
# PATH MAPPING, verified on hardware 2026-08-19:
#   libnspire's root "/" IS the device's "/documents/".
# Proof: Ndless's own source reads "/documents/themes.csv", and `nsp ls /` shows that file at
# "/themes.csv"; the ndless folder created at the top of Documents shows at "/ndless".
# So a file pushed to "/bench/x.tns" is opened by an Ndless program as "/documents/bench/x.tns".
# The C programs' hardcoded /documents/... paths are CORRECT and must not be changed -- only these
# transfer paths drop the prefix.
set -u
cd "$(dirname "$0")/../.."
# THE SCRIPT SUPPLIES ITS OWN LIBRARY PATH. tools/nspire-cli/nsp is a COMMITTED binary linked
# against an absolute path from this repo's former name (/Users/.../nspire-slm/...), which does not
# exist any more, so it aborts with a dyld error before doing anything. Exporting the variable in
# the caller's shell does NOT help: macOS SIP strips DYLD_* when it execs /bin/sh, so it never
# reaches nsp. Setting it here, after the cd, is the only place that works for every caller.
DYLD_LIBRARY_PATH="$PWD/vendor/libnspire/_install/lib:${DYLD_LIBRARY_PATH:-}"
export DYLD_LIBRARY_PATH
NSP=tools/nspire-cli/nsp
OUT=results/$(date +%Y%m%d-%H%M%S)
mkdir -p "$OUT"

for f in eval_device.txt results.txt llama2_device.txt; do
    if $NSP pull "/bench/${f}.tns" "$OUT/$f" 2>/dev/null; then :; else
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
