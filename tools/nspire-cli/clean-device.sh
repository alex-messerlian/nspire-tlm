#!/bin/sh
# clean-device.sh -- leave ONLY the files a ChatTLM demo needs on the calculator.
#
# WHY. push-all.sh used to send 12 benchmark programs, two engines and 33 MB of legacy checkpoints
# on every run. The document root reached 22 entries and chattlm could not be found among them --
# reported from the device as "I can't find it". A132 made those opt-in; this removes what earlier
# runs already left behind.
#
# WHAT SURVIVES, and why each one:
#   /chattlm.tns                     the app, opened from My Documents
#   /chattlm/startup/chattlm.tns     the copy the loader runs at power-on (ploaderhook.c:484)
#   /chattlm/ChatTLM_Setup.tns       our setup document, for a calculator with no loader yet
#   /chattlm/chattlm_support.tns     the loader itself. A134: the exploit reads this exact path
#                                    (installer/stage0.S respath) and the file was NEVER pushed,
#                                    so setup only worked where Ndless was already installed.
#   /ndless/ndless_installer_*.tns   THEIR installer, kept DELIBERATELY as a fallback, together
#   /ndless/ndless_resources.tns     with the file it needs. Ours has not been run on hardware
#                                    since the A134 path change, and removing the proven route
#                                    before that would leave a reset calculator with no way back.
#                                    Delete both once our setup has installed from scratch once.
#   /tlm/{store,tok4096,model4096}   the model data; device_app needs all three in one directory
#   /tlm/chats.tns.tns               the student's saved sessions -- user data, not ours to delete
#   /tlm/feedback.tns.tns            same
#   /themes.csv                      read by Ndless from the documents root
#
# Everything else is a benchmark, a log, or a superseded model. Nothing here is unrecoverable:
# PUSH_BENCH=1 sh tools/nspire-cli/push-all.sh restores the measurement tools in about two minutes.
set -u
cd "$(dirname "$0")/../.."
DYLD_LIBRARY_PATH="$PWD/vendor/libnspire/_install/lib:${DYLD_LIBRARY_PATH:-}"
export DYLD_LIBRARY_PATH
NSP=tools/nspire-cli/nsp
DRY="${DRY_RUN:-0}"

gone=0; kept=0; failed=0
drop() {
    if [ "$DRY" = "1" ]; then echo "  would remove $1"; gone=$((gone+1)); return; fi
    if $NSP rm "$1" >/dev/null 2>&1; then echo "  removed $1"; gone=$((gone+1))
    else echo "  (absent)  $1"; fi
}
# EMPTY ROOT-LEVEL DIRECTORIES CANNOT BE REMOVED OVER USB, and that is a device quirk rather than
# a bug here. nspire_dir_delete answers "Path does not exist" for /bench, /documents/bench, bench
# and /bench/ alike, while ns ls on the same path works and the root listing still shows it. Same
# family as this tool's own note that mkdir fails at the documents root (0xFF0F).
#
# The FILES inside are gone, which is what made chattlm unfindable. The empty folder is cosmetic
# and the calculator deletes it in two keypresses from its own file browser. Reported, not retried.
dropdir() {
    if [ "$DRY" = "1" ]; then echo "  would rmdir $1"; return; fi
    $NSP rmdir "$1" >/dev/null 2>&1 && echo "  rmdir   $1" \
        || echo "  (left empty -- root dirs cannot be removed over USB; delete on the device) $1"
}

echo "--- benchmark and engine programs (restore with PUSH_BENCH=1) ---"
for b in platform mem mac flash rtc cas forward memceiling byte word ask; do
    drop "/bench_${b}.tns"
done
drop /eval_device.tns
drop /llama2.tns
drop /golden_dev.tns

echo "--- benchmark output logs ---"
for f in results filetest llama2_device golden argtest slmlog fsprobe memceiling; do
    drop "/bench/${f}.txt.tns"
done
drop /bench/logits0.bin.tns
dropdir /bench

echo "--- superseded checkpoints (33 MB) ---"
drop /models/stories15M_q80.bin.tns
drop /models/stories15M_q96.bin.tns
drop /models/tokenizer.bin.tns
dropdir /models
dropdir /slm

echo "--- stray logs ---"
drop /tlm/caslog.txt.tns
drop /tlm/casnext.txt.tns
drop /tlm/casnext3.txt.tns
drop /ndless/slmlog.txt.tns
drop /NspireLogs.zip

echo
echo "--- what is left ---"
for d in / /chattlm /chattlm/startup /ndless /ndless/startup /tlm; do
    echo "  $d"
    $NSP ls "$d" 2>/dev/null | sed 's/^/      /'
done
echo
echo "removed $gone item(s)."
