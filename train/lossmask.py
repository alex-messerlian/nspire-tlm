"""THE loss mask. One definition, imported by every trainer.

TOOL_SPEC.md §1, "the one rule that matters most":

    The model never emits a result. The model never computes a value.
    ... If results are left in the loss, the model learns to predict plausible-looking numbers --
    which is exactly the failure this entire architecture exists to prevent.

    Span                                          In loss?
    <res> ... </res> INCLUDING DELIMITERS         no

WHY THIS FILE EXISTS. The mask was an inline loop, copied into EIGHT trainers -- select_run,
shippability, capability, l0, l2, eval_noise, refusal_run, seed_population -- with no function, no
test and no gate. Every copy had the same two defects, because they are copies.

DEFECT 1, MEASURED: THE FIRST CONTENT TOKEN OF EVERY RESULT SPAN WAS IN THE LOSS.

    pos  x (context)   y (TARGET)    in loss?
     25  '</tool>'     '<res>'       *** IN LOSS ***
     26  '<res>'       ' 12'         *** IN LOSS ***      <-- the leading digits of the result
     27  ' 12'         '.'           masked
     28  '.'           '5'           masked
     29  '5'           '</res>'      masked

    The loop was `if x[c]==RES_O: ins=True; elif x[c]==RES_C: ins=False; elif ins: mask[c]=1`. The
    `elif` chain means the position where x IS `<res>` never sets the mask -- and mask[c] gates the
    prediction of x[c+1], the first token of the number. So on every tool-using document the model
    was trained to predict the leading digits of a value it is supposed to READ. That is not a
    marginal off-by-one; it is the architecture's central rule violated at the single most
    informative token of the span.

DEFECT 2: A WINDOW THAT OPENS MID-SPAN WAS UNMASKED THROUGHOUT. Training samples a random offset
into the bin, so a window can begin between `<res>` and `</res>`. The loop starts with ins=False and
sees no opening delimiter, so every result token to the first `</res>` entered the loss. At SEQ=256
against ~96-token documents this is not rare.

Both are fixed here, once. Tested in train/test_lossmask.py, mutation-controlled, and gated.
"""
from __future__ import annotations

import numpy as np


def res_loss_mask(y: np.ndarray, res_open: int, res_close: int) -> np.ndarray:
    """1 where the TARGET is inside a result span (delimiters included), else 0.

    `y` is the target array, shape (batch, seq): y[b, c] is the token the model is asked to predict
    at position c. The mask is computed on the TARGETS, not on the context, because it is the target
    that is or is not in the loss -- computing it on the context is what produced defect 1.

    A window that opens mid-span is detected by looking for `</res>` before any `<res>` on that row,
    which is only possible if the row began inside one.
    """
    if y.ndim != 2:
        raise ValueError(f"y must be (batch, seq), got shape {y.shape}")
    mask = np.zeros_like(y, dtype=np.int64)
    for b in range(y.shape[0]):
        row = y[b]
        # DEFECT 2: seed the state. A `</res>` reached before any `<res>` means this window opened
        # inside a result span, and everything up to that close is result content.
        inside = False
        for tok in row:
            if tok == res_open:
                break
            if tok == res_close:
                inside = True
                break
        for c, tok in enumerate(row):
            if tok == res_open:
                inside = True
                mask[b, c] = 1          # the delimiter itself: TOOL_SPEC says not in loss
            elif tok == res_close:
                mask[b, c] = 1
                inside = False
            elif inside:
                mask[b, c] = 1
    return mask


IGNORE_INDEX = -100


def masked_targets(y: np.ndarray, res_open: int, res_close: int) -> np.ndarray:
    """`y` with every result-span target replaced by the cross-entropy ignore index.

    This is what the trainers want; they should not be building the mask and applying it themselves,
    because that is two places to get it wrong instead of one.
    """
    return np.where(res_loss_mask(y, res_open, res_close) == 1, IGNORE_INDEX, y)


def leak_rate(y: np.ndarray, res_open: int, res_close: int) -> float:
    """Fraction of result-span targets that would reach the loss under the OLD context-walking
    rule. Reported by the gate so a regression is a number, not a boolean."""
    correct = res_loss_mask(y, res_open, res_close)
    old = np.zeros_like(correct)
    for b in range(y.shape[0]):
        ins = False
        # the historical loop, walking the CONTEXT, reproduced here as the thing to compare against
        x = np.concatenate([[res_close], y[b][:-1]])   # context is y shifted right
        for c in range(y.shape[1]):
            if x[c] == res_open:
                ins = True
            elif x[c] == res_close:
                ins = False
            elif ins:
                old[b, c] = 1
    leaked = int(((correct == 1) & (old == 0)).sum())
    total = int((correct == 1).sum())
    return leaked / total if total else 0.0
