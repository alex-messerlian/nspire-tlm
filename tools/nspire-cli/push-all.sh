#!/bin/sh
# push-all.sh -- upload everything the device run needs, in one go.
#
# Run this with USB CONNECTED. Then UNPLUG before running anything on the calculator: the CX II
# drops to 288 MHz while tethered and every timing taken over the cable is void.
set -eu
cd "$(dirname "$0")/../.."
NSP=tools/nspire-cli/nsp

echo "--- device ---"
$NSP info

echo "--- directories ---"
$NSP mkdir /documents/bench   2>/dev/null || echo "  (bench exists)"
$NSP mkdir /documents/models  2>/dev/null || echo "  (models exists)"

echo "--- programs ---"
$NSP push tools/eval/eval_device.tns  /documents/eval_device.tns
$NSP push src/llama2.tns              /documents/llama2.tns
$NSP push bench/bench_platform.tns    /documents/bench_platform.tns
$NSP push bench/bench_mem.tns         /documents/bench_mem.tns
$NSP push bench/bench_mac.tns         /documents/bench_mac.tns
$NSP push bench/bench_flash.tns       /documents/bench_flash.tns

echo "--- model (17 MB, slow) ---"
$NSP push models/stories15M_q80.bin   /documents/models/stories15M_q80.bin.tns
$NSP push models/tokenizer.bin        /documents/models/tokenizer.bin.tns

echo
echo "Done. NOW UNPLUG USB before running anything. See docs/RUNSHEET.md for the run order."
