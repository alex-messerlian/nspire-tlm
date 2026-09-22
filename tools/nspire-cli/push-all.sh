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
# THE SCRIPT SUPPLIES ITS OWN LIBRARY PATH. tools/nspire-cli/nsp is a COMMITTED binary linked
# against an absolute path from this repo's former name (/Users/.../nspire-slm/...), which does not
# exist any more, so it aborts with a dyld error before doing anything. Exporting the variable in
# the caller's shell does NOT help: macOS SIP strips DYLD_* when it execs /bin/sh, so it never
# reaches nsp. Setting it here, after the cd, is the only place that works for every caller.
DYLD_LIBRARY_PATH="$PWD/vendor/libnspire/_install/lib:${DYLD_LIBRARY_PATH:-}"
export DYLD_LIBRARY_PATH
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
# A134b. THE LIST IS DERIVED FROM WHAT IS ACTUALLY SENT, not hardcoded.
#
# It used to name /bench /models /tlm unconditionally. Two defects in one line: /chattlm and
# /chattlm/startup were never created, so the first push after the rename failed partway through
# with "Path does not exist" -- AFTER writing /chattlm.tns; and /bench and /models were recreated
# on every run even with PUSH_BENCH unset, which is the root clutter clean-device.sh exists to
# remove. A directory is made here only if a file is going into it.
DIRS="/chattlm/startup /tlm"
[ "${PUSH_BENCH:-0}" = "1" ]  && DIRS="$DIRS /bench"
[ "${PUSH_LEGACY:-0}" = "1" ] && DIRS="$DIRS /models"
for d in $DIRS; do
    $NSP mkdir "$d/.mk" >/dev/null 2>&1 || true
    $NSP rmdir "$d/.mk" >/dev/null 2>&1 || true
    $NSP ls "$d" >/dev/null 2>&1 || { echo "  FATAL: $d does not exist"; exit 1; }
done
echo "  present:$DIRS"

# ---- GS GATE ---------------------------------------------------------------------------------
# Refuse to push a checkpoint the binary cannot load.
#
# THIS HAS NOW COST THREE DEVICE PASSES. read_checkpoint compares the file's group_size against the
# compile-time FIXED_GS and exit()s on mismatch, so the failure is total and instant: the program
# prints one line and vanishes. build/ holds BOTH a GS=32 and a GS=96 export of the same weights
# with names a glance does not distinguish -- model4096.bin and model4096_gs96.bin -- and the
# transfer set had the wrong one. Every program that loads a model died at the same line.
#
# A comment saying "use the gs96 one" is what failed twice. This is a check.
GS_WANT=$(grep -oE '^#define FIXED_GS [0-9]+' src/runq_nspire.c | grep -oE '[0-9]+$')
GS_HAVE=$(od -An -tu4 -j37 -N4 build/transfer/model4096.bin.tns 2>/dev/null | tr -d ' ')
if [ -z "$GS_WANT" ] || [ -z "$GS_HAVE" ]; then
    echo "  FATAL: could not read the group size from the binary or the checkpoint"; exit 1
fi
if [ "$GS_WANT" != "$GS_HAVE" ]; then
    echo "  FATAL: build/transfer/model4096.bin.tns is GS=$GS_HAVE but the binary is built for GS=$GS_WANT."
    echo "         read_checkpoint() will print FATAL and exit() on the device."
    echo "         build/ has both exports; copy the matching one:"
    echo "           cp build/model4096_gs96.bin build/transfer/model4096.bin.tns   # for GS=96"
    exit 1
fi
echo "  GS gate: checkpoint and binary both GS=$GS_WANT"

# ---- STALENESS GATE --------------------------------------------------------------------------
# Refuse to push a program older than the source it was built from.
#
# THE GS GATE'S SIBLING, and it has cost as much. bench_forward.tns was built once by a hand-typed
# cross-compile line -- nothing in the repo produced it -- so an edit to bench_forward.c or to
# runq_nspire.c's profiling block left a stale binary in place that pushed, ran, and reported the
# old behaviour. A stale artefact satisfies every consumer; that is what makes it worse than a
# missing one. `make bench` from the repo root now builds these; this refuses to ship what it did
# not build.
stale=0
newer_than() {   # newer_than <artefact> <source>...
    art=$1; shift
    [ -f "$art" ] || { echo "  MISSING: $art -- run: make bench"; stale=1; return; }
    for s in "$@"; do
        if [ "$s" -nt "$art" ]; then
            echo "  STALE: $art is older than $s"; stale=1
        fi
    done
}
# A132. THE FRESHNESS CHECKS FOLLOW WHAT IS ACTUALLY BEING PUSHED. Demanding a fresh benchmark
# binary on a run that does not send one is a FATAL on something the transfer does not touch, and
# the fix it prints ("make bench") is then irrelevant to what failed.
if [ "${PUSH_BENCH:-0}" = "1" ]; then
newer_than bench/bench_forward.tns bench/bench_forward.c bench/common.h src/runq_nspire.c src/nspire.c
newer_than bench/bench_cas.tns      bench/bench_cas.c bench/common.h
newer_than bench/bench_platform.tns bench/bench_platform.c bench/common.h
newer_than bench/bench_mem.tns      bench/bench_mem.c bench/common.h
newer_than bench/bench_mac.tns      bench/bench_mac.c bench/common.h
newer_than bench/bench_flash.tns    bench/bench_flash.c bench/common.h
newer_than bench/bench_rtc.tns      bench/bench_rtc.c bench/common.h
newer_than tools/eval/eval_device.tns tools/eval/device_main.c tools/eval/eval.h
newer_than src/llama2.tns             src/nspire_main.c src/runq_nspire.c src/nspire.c
fi
newer_than build/chattlm.tns        $(echo src/store/*.c src/store/*.h src/runq_nspire.c src/nspire.c)
# These two have their own Makefiles (tools/eval/, src/) and are NOT built by the repo-root `make`.
# They were absent from a fresh worktree while push-all.sh sent them unconditionally under `set -e`,
# so the transfer would have aborted mid-run after the device had already been half-written. Named
# here so the failure arrives BEFORE anything is sent, with the command that fixes it.
newer_than build/transfer/store.tns.tns     corpus/store_clean.json
newer_than build/transfer/tok4096.tok.tns   build/tok4096.tok

# A134. The loader and the setup document. Both are built outside the repo-root Makefile, and the
# loader in particular is the file the exploit reads BY HARDCODED PATH -- a stale one here is a
# calculator that installs an old ChatTLM and reports success.
newer_than build/chattlm_support.tns  resources/brand.py resources/Makefile \
                                      vendor/Ndless/ndless-sdk/lib/libsyscalls.a \
                                      vendor/Ndless/ndless-sdk/lib/libndls.a
newer_than build/ChatTLM_Setup.tns    installer/gui.lua installer/stage0.S installer/installer.lua \
                                      installer/ipc.lua installer/Problem1_template.xml

# ...AND ASK MAKE, because the hand-written lists above are a copy of the Makefiles' prerequisites
# and a copy goes stale. Measured: eval_device.tns was listed against device_main.c and eval.h only,
# while tools/eval/Makefile builds it from $(CORE) -- eleven files including dispatch.c. A change to
# dispatch.c therefore left a STALE eval_device.tns that this gate passed. Same shape as the parity
# gate comparing one field of five: a check named for a property must cover the property's parts.
#
# `make -q TARGET` exits non-zero when the target is out of date and touches nothing. It is the
# question itself rather than a proxy for it, so it cannot drift from the Makefile the way a list
# can. Kept ALONGSIDE the explicit lists rather than replacing them: this can only make the gate
# stricter, and the lists still document the intent for a reader.
ask_make() {   # ask_make <dir> <target> <label>
    ( cd "$1" 2>/dev/null && make -q "$2" >/dev/null 2>&1 ) || {
        echo "  STALE (make): $3 is out of date against its own Makefile prerequisites"; stale=1; }
}
ask_make .          build/chattlm.tns        build/chattlm.tns
ask_make resources  "$PWD/build/chattlm_support.tns" build/chattlm_support.tns
ask_make installer  ../build/ChatTLM_Setup.tns       build/ChatTLM_Setup.tns
if [ "${PUSH_BENCH:-0}" = "1" ]; then
ask_make tools/eval eval_device.tns          tools/eval/eval_device.tns
ask_make src        llama2.tns               src/llama2.tns
for _b in forward cas platform mem mac flash rtc; do
    ask_make . "bench/bench_${_b}.tns" "bench/bench_${_b}.tns"
done
fi

if [ "$stale" -ne 0 ]; then
    echo "  FATAL: at least one program is stale or missing. Pushing a stale binary measures the"
    echo "         previous session. Rebuild everything the transfer set needs:"
    echo "           make bench && make device"
    echo "           (cd tools/eval && make eval_device.tns)      # its own Makefile"
    echo "           (cd src       && make llama2.tns)            # its own Makefile"
    echo "           python3 tools/store_pack.py corpus/store_clean.json build/store.tns"
    echo "           cp build/store.tns build/transfer/store.tns.tns"
    echo "           cp build/tok4096.tok build/transfer/tok4096.tok.tns"
    echo "         then re-run this script."
    exit 1
fi
echo "  staleness gate: every program is newer than its sources"

# A132. THE BENCHMARKS AND THE LEGACY MODELS ARE OPT-IN NOW.
#
# This script pushed 12 benchmark programs, two engines and 33 MB of stories15M checkpoints on
# EVERY run. The calculator's document list reached 22 entries at the root and a student -- or a
# judge -- could not find chattlm among them. Reported as "I can't find it".
#
# A one-off tidy would not have held: the next push put all of it back, so the clutter was a
# property of this script rather than of the device. Default is now the DEMO set: the app, the
# setup document, the model data. `PUSH_BENCH=1` restores the measurement tools when a measurement
# is actually being taken, which is the only time they are needed and takes about two minutes.
if [ "${PUSH_BENCH:-0}" = "1" ]; then
echo "--- programs (PUSH_BENCH=1) ---"
send tools/eval/eval_device.tns  /eval_device.tns
send src/llama2.tns              /llama2.tns
send bench/bench_platform.tns    /bench_platform.tns
send bench/bench_mem.tns         /bench_mem.tns
send bench/bench_mac.tns         /bench_mac.tns
send bench/bench_flash.tns       /bench_flash.tns
send bench/bench_rtc.tns         /bench_rtc.tns
send bench/bench_cas.tns         /bench_cas.tns
send bench/bench_forward.tns  /bench_forward.tns
fi

# ChatTLM: the demo. Nothing staged this set before -- push-all.sh created /bench and /models and
# never touched build/transfer/, so a fresh device had no /tlm at all and the app exited at boot.
# All three data files must land, and all three must land in the SAME directory: device_app.c
# accepts a prefix only when store, tokenizer AND model all open under it.
echo "--- ChatTLM (8.2 MB: ~40 s to push, ~40 s to verify) ---"
send build/chattlm.tns             /chattlm.tns
# A130. THE STARTUP COPY IS PUSHED TOO, AND IT IS THE ONE THE CALCULATOR ACTUALLY RUNS.
#
# The loader runs every document in /chattlm/startup at boot (ploaderhook.c:484, file_each on
# "./chattlm/startup"), so a copy of the app lives there and that copy is what a student sees when
# they turn the calculator on. This script pushed ONLY /chattlm.tns.
#
# The two happened to be identical when this was noticed -- verified by pulling both and comparing
# sha256 -- so nothing was wrong yet. That is luck, not a mechanism: the next push would have
# updated the root copy and left the boot copy behind, and the device would have kept running the
# old app while every hash check on the new one passed. A stale binary that reports itself as
# freshly transferred is the exact failure this script's verify step exists to prevent, one path
# over.
#
# Pushed from the same local file, so they cannot diverge.
send build/chattlm.tns             /chattlm/startup/chattlm.tns
# A131/A134. THE SETUP DOCUMENT AND THE SUPPORT FILE IT LOADS.
#
# ChatTLM_Setup.tns is the one file a student opens on a calculator that has never run ChatTLM.
# It states the supported OS range, then runs an exploit whose payload reads exactly one path:
#   A:\documents\chattlm\chattlm_support.tns   (installer/stage0.S, respath)
#
# A134. THAT SUPPORT FILE WAS NEVER PUSHED. The setup document was described as "bundling" the
# loader and did not contain it -- it loaded a file this script never sent, so setup only ever
# worked on a calculator where Ndless had already been installed BY HAND. On a fresh one it could
# not work at all, and the failure looks like a flaky exploit rather than a missing file.
send build/ChatTLM_Setup.tns       /chattlm/ChatTLM_Setup.tns
send build/chattlm_support.tns     /chattlm/chattlm_support.tns
send build/transfer/store.tns.tns  /tlm/store.tns.tns
send build/transfer/tok4096.tok.tns /tlm/tok4096.tok.tns
send build/transfer/model4096.bin.tns /tlm/model4096.bin.tns

if [ "${PUSH_LEGACY:-0}" = "1" ]; then
echo "--- legacy stories15M model (17 MB: ~60 s to push, ~60 s to verify) ---"
send models/stories15M_q80.bin   /models/stories15M_q80.bin.tns
send models/tokenizer.bin        /models/tokenizer.bin.tns
fi

echo
if [ "$fail" -ne 0 ]; then
    echo "FAILED -- at least one file is corrupt on the device. Do not run anything."
    exit 1
fi
echo "All files verified byte-identical on device."
echo "NOW UNPLUG USB before running anything. See docs/RUNSHEET.md."
