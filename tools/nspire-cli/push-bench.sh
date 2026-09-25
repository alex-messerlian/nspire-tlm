#!/usr/bin/env sh
# push-bench.sh -- the benchmark set for a measurement session. Run AFTER push-all.sh, USB connected.
#
# Sends bench_forward, bench_sweep, bench_generate and golden_dev to the documents root, and the
# sweep shapes to /sweep. Refuses to send a device binary older than its sources: a host `make` does
# not rebuild these, so a stale one looks valid and measures a different program.
#
# FLASH: the group-88 sweep shapes (about 74 MB) and the group-32 set (about 46 MB) do not fit
# together. The old files are REMOVED BY EXACT NAME -- only the ones this project created, and all of
# them are refused by the group-32 engine anyway (rq_probe, A151). Nothing else is touched.
set -eu
cd "$(dirname "$0")/../.."
DYLD_LIBRARY_PATH="$PWD/vendor/libnspire/_install/lib:${DYLD_LIBRARY_PATH:-}"
export DYLD_LIBRARY_PATH
NSP=tools/nspire-cli/nsp
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
fail=0

stale() {    # stale <binary> <sources...>: fail if any source is newer than the binary
    b=$1; shift
    [ -f "$b" ] || { echo "  FATAL: $b missing -- run: make $b"; exit 1; }
    for s in "$@"; do
        [ "$s" -nt "$b" ] && { echo "  FATAL: $b is older than $s -- run: make $b"; exit 1; }
    done
    return 0
}
stale bench/bench_forward.tns  bench/bench_forward.c bench/common.h src/runq_nspire.c
stale bench/bench_sweep.tns    bench/bench_sweep.c bench/common.h src/runq_nspire.c
stale bench/bench_generate.tns src/store/device_generate.c src/store/gencore.c src/runq_nspire.c
stale build/golden_dev.tns     tools/eval/golden_forward.c src/runq_nspire.c

send() {     # send <local> <remote>, read back and compare
    $NSP push "$1" "$2"
    sleep 1
    if $NSP pull "$2" "$TMP/v.bin" >/dev/null 2>&1 && cmp -s "$1" "$TMP/v.bin"; then
        echo "  verified $2"
    else
        echo "  *** READ-BACK DIFFERS: $2"; fail=1
    fi
}

echo "--- removing the group-88 sweep shapes (refused by the group-32 engine) ---"
for f in m176 m264 m264h6 m352 m352b m440 m440h10; do
    $NSP rm "/sweep/$f.bin.tns" >/dev/null 2>&1 && echo "  removed /sweep/$f.bin.tns" || true
done
$NSP mkdir /sweep/.mk >/dev/null 2>&1 || true
$NSP rmdir /sweep/.mk >/dev/null 2>&1 || true

echo "--- benchmarks ---"
send bench/bench_forward.tns  /bench_forward.tns
send bench/bench_sweep.tns    /bench_sweep.tns
send bench/bench_generate.tns /bench_generate.tns
send build/golden_dev.tns     /golden_dev.tns
echo "--- sweep shapes (group 32) ---"
for f in build/sweep/*.bin.tns; do send "$f" "/sweep/$(basename "$f")"; done
$NSP ls /sweep || true
[ "$fail" = 0 ] && echo "ALL VERIFIED" || { echo "SOME FILES DID NOT VERIFY -- re-run"; exit 1; }
