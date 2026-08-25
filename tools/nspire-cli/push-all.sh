#!/bin/sh
# push-all.sh -- upload everything the device run needs, and VERIFY EVERY FILE BY HASH.
#
# Run with USB CONNECTED. Then UNPLUG before running anything on the calculator: the CX II drops to
# 288 MHz while tethered and every timing taken over the cable is void.
#
# PATH MAPPING, verified on hardware 2026-08-19:
#   libnspire's root "/" IS the device's "/documents/".
#   Proof: Ndless reads "/documents/themes.csv" and `nsp ls /` shows it at "/themes.csv".
#   So "/bench/x.tns" here is "/documents/bench/x.tns" to an Ndless program. The C programs'
#   hardcoded /documents/... paths are CORRECT; only these transfer paths drop the prefix.
#
# WHY EVERY FILE IS HASH-VERIFIED:
#   A push can report "ok", write exactly the right byte count, and still store wrong data. That
#   happened here with the 17 MB model -- correct size, wrong contents, and it would have produced
#   garbage output on device with no obvious cause. Size checks and exit codes are both insufficient.
set -eu
cd "$(dirname "$0")/../.."
NSP=tools/nspire-cli/nsp
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
fail=0

# Verify by reading back and comparing. Retry the READ, not the write.
#
# Observed twice with the 17 MB model: a read-back issued immediately after a large write returns
# data that does not match, while an independent read moments later matches exactly. The device has
# not finished committing the write when the read starts. Re-pushing on that signal is the wrong
# response -- it rewrites a file that was already correct, and doubles the time. Retry the read.
verify() {   # verify <local> <remote>
    attempt=1
    while [ "$attempt" -le 3 ]; do
        sleep 1
        if $NSP pull "$2" "$TMP/v.bin" >/dev/null 2>&1 && cmp -s "$1" "$TMP/v.bin"; then
            [ "$attempt" -eq 1 ] && echo "  verified $2" || echo "  verified $2 (read attempt $attempt)"
            return
        fi
        attempt=$((attempt + 1))
    done
    # Three clean reads all disagreed -- now it is worth suspecting the write.
    echo "  read-back disagreed 3x on $2 -- re-pushing once"
    $NSP rm "$2" >/dev/null 2>&1 || true
    $NSP push "$1" "$2" >/dev/null 2>&1 || true
    sleep 2
    if $NSP pull "$2" "$TMP/v.bin" >/dev/null 2>&1 && cmp -s "$1" "$TMP/v.bin"; then
        echo "  verified $2 (after re-push)"
    else
        echo "  *** STILL CORRUPT AFTER RE-PUSH: $2"; fail=1
    fi
}

send() {     # send <local> <remote>
    $NSP push "$1" "$2"
    verify "$1" "$2"
}

echo "--- device ---"
$NSP info

# NOTE: mkdir at the documents ROOT fails on the device (returns 0xFF0F). A two-component path
# works and creates parents, so this is how /bench and /models get made.
echo "--- directories ---"
$NSP mkdir /bench/.mk  >/dev/null 2>&1 || true
$NSP mkdir /models/.mk >/dev/null 2>&1 || true
$NSP mkdir /tlm/.mk    >/dev/null 2>&1 || true
$NSP rmdir /bench/.mk  >/dev/null 2>&1 || true
$NSP rmdir /models/.mk >/dev/null 2>&1 || true
$NSP rmdir /tlm/.mk    >/dev/null 2>&1 || true
$NSP ls /bench  >/dev/null 2>&1 || { echo "  FATAL: /bench does not exist"; exit 1; }
$NSP ls /models >/dev/null 2>&1 || { echo "  FATAL: /models does not exist"; exit 1; }
$NSP ls /tlm    >/dev/null 2>&1 || { echo "  FATAL: /tlm does not exist"; exit 1; }
echo "  /bench, /models and /tlm present"

echo "--- programs ---"
send tools/eval/eval_device.tns  /eval_device.tns
send src/llama2.tns              /llama2.tns
send bench/bench_platform.tns    /bench_platform.tns
send bench/bench_mem.tns         /bench_mem.tns
send bench/bench_mac.tns         /bench_mac.tns
send bench/bench_flash.tns       /bench_flash.tns

# ChatTLM: the demo. Nothing staged this set before -- push-all.sh created /bench and /models and
# never touched build/transfer/, so a fresh device had no /tlm at all and the app exited at boot.
# All three data files must land, and all three must land in the SAME directory: device_app.c
# accepts a prefix only when store, tokenizer AND model all open under it.
echo "--- ChatTLM (8.2 MB: ~40 s to push, ~40 s to verify) ---"
send build/chattlm.tns             /chattlm.tns
send build/transfer/store.tns.tns  /tlm/store.tns.tns
send build/transfer/tok4096.tok.tns /tlm/tok4096.tok.tns
send build/transfer/model4096.bin.tns /tlm/model4096.bin.tns

echo "--- model (17 MB: ~60 s to push, ~60 s to verify) ---"
send models/stories15M_q80.bin   /models/stories15M_q80.bin.tns
send models/tokenizer.bin        /models/tokenizer.bin.tns

echo
if [ "$fail" -ne 0 ]; then
    echo "FAILED -- at least one file is corrupt on the device. Do not run anything."
    exit 1
fi
echo "All files verified byte-identical on device."
echo "NOW UNPLUG USB before running anything. See docs/RUNSHEET.md."
