#!/usr/bin/env python3
"""Every event kind the app HANDLES must have something that EMITS it.

docs/WIRING_AUDIT.md audited checks by their CALLERS and found provenance.c had none. Its own
closing instruction was to extend that to event PRODUCERS: "for every event kind the app handles,
name the code that emits it." The extension was never made, and the defect that prompted it is
still live -- `IN_SCROLL` is handled at src/store/app.c:1900 and nothing in the repo assigns it, so
the transcript scrolls by arrow key alone and the session list not at all.

A handler with no producer reads as a feature and is not one. It is the same shape as a check with
no caller: the code exists, the reader assumes the behaviour, and nothing runs.

Exit 0 clean, 1 on a violation, 2 if it cannot run.
"""
import os, pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
os.chdir(ROOT)

HDR = ROOT / "src/store/app.h"
if not HDR.exists():
    print(f"CANNOT CHECK: {HDR} is missing")
    sys.exit(2)

# The enum is the roster: every kind it declares must be handled AND produced, or explained.
m = re.search(r"typedef\s+enum\s*\{([^}]*)\}\s*in_kind\s*;", HDR.read_text())
if not m:
    print("CANNOT CHECK: could not find the in_kind enum in src/store/app.h")
    sys.exit(2)
KINDS = [k.split("=")[0].strip() for k in m.group(1).split(",") if k.strip()]

# IN_NONE is the absence of an event; it is produced by initialisation, not by a device.
EXEMPT = {"IN_NONE": "the absence of an event -- produced by zero-initialisation, handled by falling through"}

# KNOWN DEFECTS, named so the gate stays meaningful rather than permanently red. Each must have a
# reason and the list must SHRINK. A gate that is always failing is a gate people stop reading; a
# gate that names three known defects and fails on a fourth is one they act on.
KNOWN_BAD = {
    "IN_SCROLL": "handled in app.c since the day it was written and emitted by nothing, so the "
                 "transcript scrolls by arrow key alone and the session list not at all. Fixing it "
                 "means deciding what gesture produces it on the touchpad, which cannot be verified "
                 "without the device -- and a device pass is pending. NOT fixed here on purpose: "
                 "changing input handling immediately before a hardware run is how a pass gets lost.",
}

srcs = list((ROOT / "src/store").glob("*.c")) + list((ROOT / "include").glob("*.c"))
text = {p: p.read_text() for p in srcs}

def handled(kind):
    return [f"{p.name}:{i+1}" for p, s in text.items()
            for i, ln in enumerate(s.split("\n"))
            if re.search(rf"(case\s+{kind}\b|==\s*{kind}\b|{kind}\s*==)", ln)]

def produced(kind):
    # an ASSIGNMENT of the kind, or a return of it -- not a comparison
    return [f"{p.name}:{i+1}" for p, s in text.items()
            for i, ln in enumerate(s.split("\n"))
            if re.search(rf"(kind\s*=\s*{kind}\b|return\s+{kind}\b)", ln)]

bad, known_seen = [], set()
print(f"  {'event kind':12} {'handled at':28} {'produced at'}")
for k in KINDS:
    h, pr = handled(k), produced(k)
    note = "" if k not in EXEMPT else "  (exempt)"
    print(f"  {k:12} {(h[0] if h else '-- NOWHERE --'):28} {(pr[0] if pr else '-- NOWHERE --')}{note}")
    if k in EXEMPT:
        continue
    broken = (h and not pr) or (pr and not h)
    if broken and k in KNOWN_BAD:
        known_seen.add(k); continue
    if h and not pr:
        bad.append(f"{k}: HANDLED at {h[0]} and PRODUCED NOWHERE -- the handler cannot run")
    if pr and not h:
        bad.append(f"{k}: PRODUCED at {pr[0]} and HANDLED NOWHERE -- the event is discarded")

_stale = set(KNOWN_BAD) - known_seen
if _stale:
    print()
    for k in sorted(_stale):
        print(f"  {k} is on the known-defect list and is no longer broken -- REMOVE IT from the list")
    sys.exit(1)
if KNOWN_BAD:
    print()
    print(f"  {len(KNOWN_BAD)} KNOWN DEFECT(S), named and not fixed:")
    for k, why in sorted(KNOWN_BAD.items()):
        print(f"    {k}: {why}")

if bad:
    print()
    for b in bad:
        print(f"  {b}")
    sys.exit(1)
print("  clean: every handled kind has a producer and every produced kind has a handler")
