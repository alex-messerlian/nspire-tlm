#!/usr/bin/env python3
"""Tests for THE loss mask -- TOOL_SPEC section 1, the rule the architecture rests on.

Every case names the defect it guards. Two of them are the defects the eight inline copies had, and
they are written as the historical loop's output so a revert is caught by value, not by shape.
"""
import sys, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import numpy as np
from lossmask import res_loss_mask, masked_targets, IGNORE_INDEX

RO, RC = 9, 10          # <res>, </res>
fails = 0

def T(label, got, want):
    global fails
    ok = np.array_equal(np.asarray(got), np.asarray(want))
    if not ok:
        fails += 1
        print(f"  FAIL  {label}\n        got  {np.asarray(got).tolist()}\n        want {np.asarray(want).tolist()}")
    else:
        print(f"  PASS  {label}")

# ---- the shape of a real document -------------------------------------------------------------
#            </tool> <res>  '12'   '.'   '5'  </res>  <a>   '12'
y = np.array([[  8,    RO,   101,  102,  103,   RC,   11,   101]])

# DEFECT 1. The first content token of the span must be masked. Under the historical loop it was
# not, and it is the leading digits of the number.
T("the whole span is masked, delimiters included",
  res_loss_mask(y, RO, RC), [[0, 1, 1, 1, 1, 1, 0, 0]])

T("targets outside the span survive",
  masked_targets(y, RO, RC)[0][[0, 6, 7]], [8, 11, 101])

T("every target inside the span becomes the ignore index",
  masked_targets(y, RO, RC)[0][1:6], [IGNORE_INDEX] * 5)

# DEFECT 2. A window that opens mid-span. There is no <res> in this row -- the document's opener is
# behind the window -- so a rule that waits for one leaves the result content in the loss.
mid = np.array([[102, 103, RC, 11, 101]])
T("a window opening mid-span masks to the close",
  res_loss_mask(mid, RO, RC), [[1, 1, 1, 0, 0]])

# A window that opens mid-span AND closes and reopens.
both = np.array([[102, RC, 11, 8, RO, 55, RC, 12]])
T("mid-span open, then a second complete span",
  res_loss_mask(both, RO, RC), [[1, 1, 0, 0, 1, 1, 1, 0]])

# ---- things that must NOT be masked ------------------------------------------------------------
T("a document with no result span masks nothing",
  res_loss_mask(np.array([[1, 2, 3, 11, 4]]), RO, RC), [[0, 0, 0, 0, 0]])

T("a refusal document -- no call, no res -- masks nothing",
  res_loss_mask(np.array([[1, 2, 11, 3, 4, 12]]), RO, RC), [[0, 0, 0, 0, 0, 0]])

# An empty span: <res></res> adjacent.
T("an empty result span masks both delimiters",
  res_loss_mask(np.array([[8, RO, RC, 11]]), RO, RC), [[0, 1, 1, 0]])

# Two spans in one window (a retry).
two = np.array([[RO, 50, RC, 11, RO, 60, RC, 12]])
T("two spans in one window",
  res_loss_mask(two, RO, RC), [[1, 1, 1, 0, 1, 1, 1, 0]])

# ---- batch independence: one row's state must not leak into the next --------------------------
batch = np.array([[8, RO, 50, 51, 52], [1, 2, 3, 4, 5]])
T("row state does not leak across the batch",
  res_loss_mask(batch, RO, RC), [[0, 1, 1, 1, 1], [0, 0, 0, 0, 0]])

# ---- the historical loop, reproduced, to show what it did -------------------------------------
def historical(y, ro, rc):
    m = np.zeros_like(y)
    for b in range(y.shape[0]):
        ins = False
        x = np.concatenate([[rc], y[b][:-1]])
        for c in range(y.shape[1]):
            if x[c] == ro: ins = True
            elif x[c] == rc: ins = False
            elif ins: m[b, c] = 1
    return m

h = historical(y, RO, RC)
if h[0][1] == 1 or h[0][2] == 1:
    fails += 1
    print("  FAIL  the historical loop was supposed to leak the first content token; it did not.\n"
          "        Either the reproduction is wrong or the defect was never there -- check before trusting this suite.")
else:
    print(f"  PASS  {'the historical loop DID leak the first content token':52} "
          f"(it masked {int(h.sum())} of {int(res_loss_mask(y,RO,RC).sum())})")

# ---- NO NINTH COPY. The defect recurred eight times because there was no importable mask, so
# every trainer that needed one wrote it. tools/eval/genloop.py's guard, applied here: any file
# reimplementing the loop is a failure, and the allowlist is this module and its own test.
import re as _re
ALLOW = {"train/lossmask.py", "train/test_lossmask.py"}
LOOP = _re.compile(r"elif\s+(ins|inside)\s*:\s*\w+\[")
_root = pathlib.Path(__file__).resolve().parent.parent
offenders = []
for f in sorted(_root.rglob("*.py")):
    rel = f.relative_to(_root).as_posix()
    if rel in ALLOW or "/.git/" in rel or rel.endswith(".bak") or "scratchpad" in rel: continue
    try: txt = f.read_text()
    except Exception: continue
    if LOOP.search(txt) and "RES_O" in txt:
        offenders.append(rel)
if offenders:
    fails += 1
    print(f"  FAIL  a file reimplements the loss mask instead of importing it: {offenders}")
else:
    print(f"  PASS  {'no file reimplements the mask -- import it, do not copy it':52}")

print(f"test_lossmask {'FAIL' if fails else 'PASS'}  ({fails} failure(s))")
sys.exit(1 if fails else 0)
