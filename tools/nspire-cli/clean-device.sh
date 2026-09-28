#!/bin/sh
# clean-device.sh -- leave ONLY the folder a student gets: /chattlm.
#
# WHAT SURVIVES (2026-09-27: one folder, the layout the public release ships):
#   /chattlm/ChatTLM_Setup.tns     the document a student opens once after every reset. It installs
#                                  the loader and closes; the home screen says "ChatTLM is ready".
#   /chattlm/chattlm_support.tns   the loader. The exploit reads this exact path (installer/stage0.S,
#                                  respath), so it cannot move.
#   /chattlm/ChatTLM.tns           ChatTLM, which the student opens from My Documents. NOT in
#                                  /chattlm/startup: the loader runs that folder inside Setup, where
#                                  the model cannot get its memory (2026-09-27; see push-all.sh).
#   /chattlm/data/                 the model, tokenizer and record store, and the student's saved
#                                  chats and feedback (device_app.c looks here first).
#   /themes.csv                    the calculator's own file, not ours. Ndless only reads it in an SDK
#                                  sample (samples/newlib-c++/test_newlib.cpp); the loader never does.
#
# WHAT GOES: the top-level copy of the app, and the old one in /chattlm/startup; /tlm, after its
# saved chats move to /chattlm/data; the benchmark programs and their /sweep shapes; the upstream
# Ndless installer pair (ChatTLM Setup has installed from nothing since 2026-09-21, and the host
# keeps a copy in ndless/); the OS's NspireLogs.zip. Every file removed is first pulled to
# attic/calculator-removed-<date>/ on the host, and the benchmarks come back with
# tools/nspire-cli/push-bench.sh.
#
# Run tools/nspire-cli/push-all.sh FIRST, so /chattlm/data is complete. This script refuses to empty
# /tlm otherwise. DRY_RUN=1 lists what it would do and changes nothing.
set -u
cd "$(dirname "$0")/../.."
DYLD_LIBRARY_PATH="$PWD/vendor/libnspire/_install/lib:${DYLD_LIBRARY_PATH:-}"
export DYLD_LIBRARY_PATH
NSP=tools/nspire-cli/nsp
DRY="${DRY_RUN:-0}"
KEEP="attic/calculator-removed-$(date +%Y-%m-%d)"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

gone=0
# Pull to the host, then remove. A file that cannot be pulled is left where it is.
drop() {
    if [ "$DRY" = "1" ]; then echo "  would remove $1"; gone=$((gone+1)); return; fi
    $NSP ls "$(dirname "$1")" 2>/dev/null | awk '{print $3}' | grep -Fqx "$(basename "$1")" \
        || { echo "  (absent)  $1"; return; }
    mkdir -p "$KEEP$(dirname "$1")"
    if ! $NSP pull "$1" "$KEEP$1" >/dev/null 2>&1; then echo "  KEPT (could not back up) $1"; return; fi
    if $NSP rm "$1" >/dev/null 2>&1; then echo "  removed $1"; gone=$((gone+1))
    else echo "  could not remove $1"; fi
}
# Move one file to a new path on the device: pull, push, read back, compare, then remove the old one.
move() {
    if [ "$DRY" = "1" ]; then echo "  would move $1 -> $2"; return; fi
    $NSP pull "$1" "$TMP/f" >/dev/null 2>&1 || { echo "  (absent)  $1"; return; }
    $NSP push "$TMP/f" "$2" >/dev/null 2>&1 && $NSP pull "$2" "$TMP/g" >/dev/null 2>&1 \
        && cmp -s "$TMP/f" "$TMP/g" || { echo "  COULD NOT MOVE $1 (left in place)"; return; }
    mkdir -p "$KEEP$(dirname "$1")"; cp "$TMP/f" "$KEEP$1"
    $NSP rm "$1" >/dev/null 2>&1 && echo "  moved $1 -> $2" || echo "  copied $1 -> $2 (old copy not removed)"
}
# EMPTY TOP-LEVEL FOLDERS CANNOT BE REMOVED OVER USB: nspire_dir_delete answers "Path does not exist"
# for them while ls on the same path works. The files inside are gone; the empty folder is deleted on
# the calculator in two keypresses. Reported, not retried.
dropdir() {
    if [ "$DRY" = "1" ]; then echo "  would rmdir $1"; return; fi
    $NSP rmdir "$1" >/dev/null 2>&1 && echo "  rmdir   $1" \
        || echo "  (empty, delete it on the calculator) $1"
}

echo "--- /chattlm/data must be complete before anything in /tlm is touched ---"
for f in store.tns.tns tok4096.tok.tns model4096.bin.tns; do
    $NSP ls /chattlm/data 2>/dev/null | awk '{print $3}' | grep -Fqx "$f" \
        || { echo "  FATAL: /chattlm/data/$f is missing. Run tools/nspire-cli/push-all.sh first."; exit 1; }
done
echo "  ok: store, tokenizer and model are in /chattlm/data"

echo "--- the student's saved chats and feedback follow the data ---"
for f in chats.tns.tns feedback.tns.tns; do
    if $NSP ls /chattlm/data 2>/dev/null | awk '{print $3}' | grep -Fqx "$f"; then
        drop "/tlm/$f"          # /chattlm/data already has one; the /tlm copy is backed up to the host
    else
        move "/tlm/$f" "/chattlm/data/$f"
    fi
done

echo "--- the top-level copy of the app, and /tlm ---"
drop /chattlm.tns
for f in store.tns.tns tok4096.tok.tns model4096.bin.tns genlog.txt.tns bench_results.txt.tns \
         golden_dev.txt.tns logits0.bin.tns tlmlog.txt.tns caslog.txt.tns casnext.txt.tns casnext3.txt.tns; do
    drop "/tlm/$f"
done
dropdir /tlm

echo "--- benchmark programs and their shapes (restore with push-bench.sh) ---"
for b in platform mem mac flash rtc cas forward generate sweep memceiling byte word ask; do
    drop "/bench_${b}.tns"
done
drop /eval_device.tns
drop /llama2.tns
drop /golden_dev.tns
for s in m176 m192 m256 m264 m264h6 m320 m352 m352b m384 m440 m440h10; do
    drop "/sweep/$s.bin.tns"
done
dropdir /sweep

echo "--- the app's old place, which the loader started inside Setup ---"
drop /chattlm/startup/chattlm.tns
dropdir /chattlm/startup

echo "--- the upstream Ndless installer pair, and the OS's log bundle ---"
drop /ndless/ndless_installer_4.5.5-6.2.0-6.4.0.tns
drop /ndless/ndless_resources.tns
dropdir /ndless
drop /NspireLogs.zip

echo
echo "--- what is left ---"
for d in / /chattlm /chattlm/data; do
    echo "  $d"
    $NSP ls "$d" 2>/dev/null | sed 's/^/      /'
done
echo
echo "removed $gone item(s); each is backed up in $KEEP/."
