#!/usr/bin/env sh
# pull-session.sh <tag> -- pull this session's device logs into results/device_<tag>/. USB connected.
# bench_forward and bench_sweep append to one log; bench_generate and golden_dev have their own.
set -eu
cd "$(dirname "$0")/../.."
DYLD_LIBRARY_PATH="$PWD/vendor/libnspire/_install/lib:${DYLD_LIBRARY_PATH:-}"
export DYLD_LIBRARY_PATH
NSP=tools/nspire-cli/nsp
TAG="${1:?usage: pull-session.sh <tag>}"
OUT="results/device_$TAG"; mkdir -p "$OUT"
for f in bench_results genlog golden_dev; do
    if $NSP pull "/tlm/$f.txt.tns" "$OUT/$f.txt" >/dev/null 2>&1; then
        echo "  pulled /tlm/$f.txt.tns -> $OUT/$f.txt ($(wc -c < "$OUT/$f.txt") B)"
    else
        echo "  NOT FOUND: /tlm/$f.txt.tns"
    fi
done
grep -h "power=\|battery, VALID\|INVALID" "$OUT"/*.txt 2>/dev/null | sort | uniq -c || true
