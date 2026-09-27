# Does the model compute the right answer? Nothing measured it until now.

Found 2026-09-25 while drafting the paper's quality section. Every computation figure this repo
had quoted measured something narrower, and three further differences between the host harness and
the calculator each moved the number by 9 to 20 points. This file records what each one was, the
instrument that replaced it, and the numbers that replace the old ones.

## 1. The label: `d1` is a refusal arm, not "grounded computation"

`FACTS.md` s8/s9, `docs/paper/history/DRAFT.md`, `docs/RESULT_WIDTH_LADDER.md` and `docs/PREREG_WIDTH_LADDER.md`
all called `d1` "grounded computation" and the ladder's headline was "a 3.5x cut costs grounded
computation 2.5 points". `tools/eval/score_arms.py` defines it otherwise, in its own docstring:

> d1: The record ANSWERS the question and one needed given is WITHHELD, so the model must refuse on
> the BINDING alone.

It is graded by `refusal_strict`, and its canonical correct output is *"I cannot answer that: F is
not given."* `d1_zero` is the same with no values at all. **Both measure declining when a value is
missing** -- which the prompt already states, because `ns_assemble` computes the unbound variables and
writes them into the record span as `missing:X`. The pre-registration inherited the mislabel, so the
ladder's pre-registered test was of that capability, not of computation.

**Retracted:** "grounded computation 100%", "-2.5 pp on grounded computation", "-1.7 pp over two
seeds on grounded computation", and the DRAFT abstract's "100% on grounded computation across 222
items". The underlying numbers stand as numbers about declining.

## 2. The metric: no arm compared an answer with the right answer

`score_arms`' `answered` count on `answer_0/s/w/x` is `well_formed AND not is_refusal AND
answer_matches_result`, and `answer_matches_result` asks only whether the stated answer is a correct
rounding of **whatever the tool returned**. A call built from a wrong value, or the wrong relation,
passes. No arm checked the call.

**Instrument:** `tools/eval/score_correct.py`. CORRECT = well-formed, not a refusal, the LAST `<res>`
within 0.1% of a REFERENCE, and the stated answer a correct rounding of it. The reference is the
record's right-hand side with the question's givens (and the store's `cval` constants) substituted,
executed by `evalcli` -- the device's evaluator, so there is no second implementation of the maths.
Controls run on every invocation: the reference's own document scores CORRECT on 120/120 items of
every arm, and the same document with one given scaled by 1.1 is rejected on 116-119 of 120 (the
survivors are relations insensitive to that variable at that value). Unscoreable items are counted
and printed; there were none.

## 3. Three ways the host harness was not the calculator

Each was found by asking what the device does differently, and each moved the shipped model's
`answer_0` correctness by 9-20 points:

| harness | prompt | decoding | weights | `answer_0`, strict |
|---|---|---|---|---|
| score_arms style | split text | T=0.8, 3 seeds | fp32 | 52.5% |
| + device prompt assembly | `build/devasm` | T=0.8, 3 seeds | fp32 | 73.1% |
| + greedy, as the device decodes | `build/devasm` | argmax | fp32 | 84.2% |
| + the calculator's int8 engine | `build/devasm` | argmax | **int8, group 88** | **75.0%** |

1. **Prompt shape.** The split's record spans predate the change that put the asked quantity's unit
   in the span (`I_S:A`), and the device strips the givens out of the question and re-appends them
   (`Given I_P = 8.54, ...`). 99.5% of the training corpus's compute documents carry the unit.
   `build/devasm` (`tools/eval/devasm.c`) runs `ask_build` + `ns_assemble` exactly as `app_request`
   does, for the record the student picked.
2. **Decoding.** `device_app.c` decodes by argmax. `score_arms` samples at T=0.8 to match the
   corpus. Sampling costs 11 points on the shipped model and 20-30 on d176.
3. **Weights.** The calculator runs int8. `build/int8gen` (`tools/eval/int8gen.c`) runs the shipped
   `runq_nspire.c` through a copy of `app_request`'s loop -- the `<res>` mask, tool execution by
   `toolrun.c`, the device's consume-then-replace injection, the 90-step cap -- and applies the
   shipped `answer_states_result`, lifted verbatim by `sed` (`build/ansmatch_impl.h`). Checked against
   the calculator: on the prompt the device logged, its first call is token-identical to the one the
   device emitted. `build/transfer/model4096.bin.tns` is byte-identical (`cmp`) to `train/ship.pt`
   quantised at group 88.

## 4. The int8 engine drops the tail of every FFN row at group 88

The fourth step above lost 9.2 points, and d176 collapsed under it (fp32 48.3% -> int8 6.7%). Cause,
in `src/runq_nspire.c`:

- `quantize(qx, x, n)` quantises `n / GS` groups and ignores the remainder;
- `matmul(xout, x, w, n, d)` walks each row with `for (j = 0; j <= n - GS; j += GS)` and ignores the
  remainder, and reads the scale as `w->s[(in + j) / GS]` with `in = i * n`.

Both assume `GS` divides the ROW length `n`. The FFN down-projection has `n = hidden_dim`: 1024 at
d352 and 512 at d176, and 88 divides neither. So every layer ignores the last 56 of 1,024 hidden
units at d352 (72 of 512 at d176), and because rows do not start on a group boundary in the flattened
tensor, part of each chunk is scaled by the neighbouring group's factor. `tools/legacy_to_q80.py`
chose 88 because it divides every TENSOR length (176 x 512 = 88 x 1024) -- a proxy for the property
the engine needs, which is that it divides every ROW length. `rq_probe` checks head size and
`dim % heads` and not this.

**Control:** the same engine source and weights at group 16, which divides every row length at both
widths (`build/int8gen_g16`, `Q80_GROUP=16`):

| `answer_0` | group 88 (shipped) | group 16 |
|---|---|---|
| shipped d352, strict | 75.0% | **84.2%** -- equal to fp32 greedy |
| shipped d352, device check | 91.7% | **96.7%** |
| d176 seed 1, device check | 11.7% | **65.0%** |

With rows aligned, int8 reproduces fp32 exactly on the strict metric. The defect is the whole gap.

**Not fixed here.** A fix changes the shipped engine's hot loop and file format (or the model's
hidden width) and needs a device session. `tools/legacy_to_q80.py` now prints a WARNING naming the
skipped inputs whenever a group does not divide a row length.

## 5. The strict grader penalises truncation, and one correct rounding

Of the shipped model's 22 `answer_0` items that call correctly and misstate the result (int8,
group 88), 20 state a number within 2% of it: `4737.6` as "4737 N", `0.01586666667` as "0.0158 W".
And `16667.8894` as "16670 V" is a correct 4-significant-figure rounding that
`grade.answer_matches_result` rejects, because it counts an integer's trailing zero as significant.
The paper's headline therefore uses the criterion the calculator itself applies,
`answer_states_result` (a stated number within 2% of the result), and reports the strict figure
beside it. On the device, an answer that fails that check has the correct value appended beneath it
by the runtime ("The calculator computed N").

## 6. Numbers

### The shipped model (int8, as on the calculator)

| item set | n (relations) | group 88, as shipped | group 16 |
|---|---|---|---|
| values given (`answer_0`), right | 120 (88) | **91.7%** | 96.7% |
| same, strict | | 75.0% | 84.2% |
| same, correct value shown incl. runtime correction | | 93.3% | 96.7% |
| quantity named by symbol (`answer_s`) | 120 (82) | 95.8% | 97.5% |
| quantity named in words (`answer_w`) | 120 (75) | 89.2% | 96.7% |
| one irrelevant value (`answer_x`) | 120 (84) | **57.5%** | 66.7% |
| decline: value withheld (`d1`) | 120 (87) | 99.2% | 100.0% |
| decline: no values (`d1_zero`) | 102 (74) | 99.0% | 100.0% |
| decline: record does not fit (`fit`) | 120 (79) | 0.0% | 0.0% |
| explain, form only (`explain`) | 87 (71) | 85.1% | 93.1% |

`refuse`, `fit_ho` and `explain_ho` are UNREACHABLE on the device: none of their records is in the
shipped store, so no student can select them. They are no longer quoted.

### Width ladder (`tools/paper/ladder_table.py`)

Scored at group 16 so the row-alignment defect does not confound width (at 88 it drops 72 of 512
hidden inputs at d176 against 56 of 1,024 at d352):

```
group 16: arm                         d176 s1  d176 s2  d352 s1  d352 s2   d176   d352  change
          decline: value withheld        99.2    100.0     99.2    100.0   99.6   99.6    +0.0
          decline: no values            100.0    100.0    100.0    100.0  100.0  100.0    +0.0
          compute: values given          65.0     83.3     95.0     92.5   74.2   93.8   -19.6
          compute: symbol-named          70.8     77.5     99.2     98.3   74.2   98.8   -24.6
          compute: word-named            75.0     80.8     96.7     95.8   77.9   96.2   -18.3
          compute: extra value           41.7     55.8     63.3     52.5   48.8   57.9    -9.2
          explain (form)                 64.4     52.9     93.1     93.1   58.6   93.1   -34.5
          judge fit                       0.0      0.0      0.0     15.8    0.0    7.9    -7.9
```

For comparison, group 88 as shipped -- the defect collapses d176 on every arm:

```
group 88: arm                         d176 s1  d176 s2  d352 s1  d352 s2   d176   d352  change
          decline: value withheld        91.7     95.8     98.3     99.2   93.8   98.8    -5.0
          decline: no values             82.4     92.2    100.0    100.0   87.3  100.0   -12.7
          compute: values given          11.7     27.5     91.7     91.7   19.6   91.7   -72.1
          compute: symbol-named          21.7     29.2     98.3     90.8   25.4   94.6   -69.2
          compute: word-named            15.0     19.2     95.8     94.2   17.1   95.0   -77.9
          compute: extra value            8.3     13.3     59.2     51.7   10.8   55.4   -44.6
          explain (form)                  2.3      0.0     85.1     83.9    1.1   84.5   -83.3
          judge fit                       0.0      0.0      0.0     18.3    0.0    9.2    -9.2
```

Read / copy / recall: declining (a field the runtime wrote) does not move; being right falls ~20
points, and the strict breakdown puts the loss in the CALL (wrong-value calls 1.7% / 5.0% at d352 vs
32.5% / 13.3% at d176, `results/ladder_g16_breakdown.txt`); explanation form falls 34.5 points. Fit
is at or near floor at both widths.

**Pre-registration.** `docs/PREREG_WIDTH_LADDER.md` named `d1`, which measures declining, and was
scored by the replaced harness. The ladder is therefore NOT reported as a test of those predictions.


## Reproduce

    tools/eval/score_on_device_decoder.sh 88 train/ship.pt train/w352.pt train/w352s2.pt train/w176.pt train/w176s2.pt
    tools/eval/score_on_device_decoder.sh 16 train/ship.pt train/w352.pt train/w352s2.pt train/w176.pt train/w176s2.pt
    .venv-tok/bin/python tools/eval/int8_calls.py results/correct_devg_ship.json


## 7. Checks added after the first draft, and what they measured

The measurements:

**Strict grader: an integer's trailing zeros are placeholders.** `grade.answer_matches_result` read
"16670" as five significant figures and rejected a correct 4-sf restatement of 16667.8894. Fixed in
`tools/eval/grade.py`; `tools/eval/test_scope.py` pins it with three cases (16670 passes, 4737 for
4737.6 still fails as truncation, 16600 for 16667.9 still fails). The report now RECOMPUTES
correctness from saved generations, so every saved run picks the fix up. Strict `answer_0`, shipped:
group 88 **79.2%** (was 75.0), group 16 **88.3%** (was 84.2), fp32 greedy 88.3%. At group 16 int8 and
fp32 reach the same verdict on **120/120** items and identical text on 114/120.
Harness steps under the fixed grader: split text 55.0 -> device prompts 76.1 -> greedy 88.3 -> int8
group 88 79.2.

**Independent references** (`tools/eval/independent_refs.py`): all 480 answer-arm references
recomputed in Python `math` from the same substituted expression agree with evalcli to 1e-8 (max
4.9e-10, evalcli's 10-sf printing). Scope: arithmetic only; a wrong stored formula would pass both.

**Paired distractor** (`tools/eval/paired_distractor.py`): the 120 `answer_0` items, each with one
spare assignment (drawn from `answer_x`'s own spares, never a symbol in the item) inserted before
the first given or after the last, nothing else changed. Device-check right:

| decoder | base | spare LAST | spare FIRST | lost (last / first) | wrong with spare in call (first) |
|---|---|---|---|---|---|
| int8 group 88 (shipped) | 110 | 104 | **55** | 8 / **59** | 43 of 65 |
| int8 group 16 | 116 | 113 | **61** | 4 / 56 | 43 of 59 |

Reversing the relevant givens, no spare: 83/91 -> 83/91 at group 88 (3 lost, 3 gained) -- the model
binds by NAME. **Cause, measured in the ladder corpus (`acdc7b70`): of 75,618 compute documents with
a spare given, 100.00% place every spare after every relevant given.** The model learned where
irrelevant values sit. CORRECTED (A158): this said the `answer_x` split "places its spare first". It
does not -- answer_control.py shuffles the order, so the spare is first in 47 of 120 items, middle
in 32, last in 41, and 79 of 120 (not last) are a shape the generator never emits. Split by position
(`tools/eval/spare_position.py`), the shipped engine is right on 41/41 with the spare last, 24/47 first
and 13/32 in the middle: the positional finding again, on items the paired experiment did not touch.

**Deterministic baseline** (`tools/eval/score_deterministic.py`): the runtime's own parse/bind via the
device prompt, evalcli, no model; the same graders. answer_0/s/w 120/120 each, answer_x 119/120,
d1 120/120, d1_zero 102/102, fit 0/120, explain format 68/87 (78.2%; 13 of 19 misses are its
keyword intent rule). On every set that asks for a number or a refusal it matches or beats the model.

**Device parser defect found by the baseline:** `ask_build` matches symbols case-insensitively, so a
stated `Q = 1.29e-05` (the answer_x spare) was bound to `q`, overriding the electron-charge constant.
One item; the model saw the same wrong prompt. Not fixed here.

**Cost model regenerated from ONE block with windows at their mean positions**
(`tools/paper/cost_model.py` -> `results/cost_model_paper.txt`): depth 46,116 + 62,144 L (R^2 0.999999);
filled position 404,009 + 1,649.8 p (R^2 0.999992; intercept extrapolated), empty 1,177.7 p (+40.1%
filled vs empty); consistency 0.32 / 0.35 / 0.05%; decode prediction -0.5% (33 tokens, 3 runs) and
-0.7% (24 tokens, 1 run), empty-cache model -5.5 / -5.3%. The width sweep's "@8" and "@256" are
positions 11-18 and 259-266.
