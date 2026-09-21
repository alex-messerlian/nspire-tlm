#!/usr/bin/env sh
# push-cas-backup.sh -- everything the BACKUP calculator needs for the CAS hypothesis sweep.
#
# SEPARATE from push-all.sh on purpose. This unit is not carrying the project: it gets Ndless and
# one 137 KB probe, and nothing else. No model, no store, no tokenizer, no ChatTLM. If a wrong
# pointer corrupts OS state on this unit, what is lost is a stock calculator and an hour.
#
# PATH MAPPING, verified on hardware 2026-08-19: libnspire's root "/" IS the device's "/documents/".
# So "/ndless/x.tns" here is "/documents/ndless/x.tns" to the OS.
#
# Run with USB CONNECTED to the BACKUP unit. Check `nsp info` says 6.4.0.74 before starting -- the
# probe reads syscall numbers that are resolved per OS version, and a different version is a
# different experiment.
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
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
fail=0

verify() {
    attempt=1
    while [ "$attempt" -le 3 ]; do
        sleep 1
        if $NSP pull "$2" "$TMP/v.bin" >/dev/null 2>&1 && cmp -s "$1" "$TMP/v.bin"; then
            echo "  verified $2"; return
        fi
        attempt=$((attempt + 1))
    done
    echo "  *** CORRUPT ON DEVICE: $2"; fail=1
}
send() { $NSP push "$1" "$2"; verify "$1" "$2"; }

echo "--- device ---"
$NSP info
echo
echo "CONFIRM the line above says OS 6.4.0.74 before continuing."
echo "A different OS resolves these syscalls to different addresses, or not at all."
echo

echo "--- directories ---"
# mkdir at the documents ROOT fails on this device (0xFF0F); a two-component path works.
$NSP mkdir /ndless/.mk >/dev/null 2>&1 || true
$NSP mkdir /tlm/.mk    >/dev/null 2>&1 || true
$NSP rmdir /ndless/.mk >/dev/null 2>&1 || true
$NSP rmdir /tlm/.mk    >/dev/null 2>&1 || true
$NSP ls /ndless >/dev/null 2>&1 || { echo "  FATAL: /ndless does not exist"; exit 1; }
echo "  /ndless and /tlm present"

echo "--- Ndless r2022 installer ---"
send ndless/ndless_installer_4.5.5-6.2.0-6.4.0.tns /ndless/ndless_installer_4.5.5-6.2.0-6.4.0.tns
send ndless/ndless_resources.tns                   /ndless/ndless_resources.tns

echo "--- the probe ---"
send bench/bench_cas.tns /bench_cas.tns

echo
[ "$fail" -ne 0 ] && { echo "FAILED -- a file is corrupt on the device. Do not run anything."; exit 1; }
echo "All files verified byte-identical on the backup unit."
echo "Next: docs/RUNSHEET_CAS_BACKUP.md"
