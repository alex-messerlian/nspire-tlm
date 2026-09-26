# The shipped app, end to end: the selector was the bottleneck, not the model

Measured 2026-09-25 (A156). Every quality number before this one handed the model the item's own
record. Since A125 the app picks the relation itself and declines when it is not confident, so a
student's answer is the product of two stages, and nothing had measured the first.

## Instrument

`tools/eval/score_endtoend.py`, on the same items as `score_correct.py` and `score_int8_arms.py`:

- **selection**: `build/autoasm` `#include`s `app.c` and calls `open_picker()`, the function the
  enter key reaches on the calculator. The host stub for `app_request` records the record id it was
  sent. Only the prompt assembly after that is replayed, in `app_request`'s order, which is devasm's
  sequence and is parity-checked against the calculator's logged prompts.
- **generation**: `build/int8gen`, the shipped int8 file (padded group 88) through the app's own
  generation loop (`gencore.c`), greedy.
- **grading**: answer arms use score_correct's device check against the item's REFERENCE (never
  against the chosen record, so a lucky wrong choice would count, and is counted apart: it happened
  0 times); decline arms are right when the output declines; `explain` is right only if the right
  relation was chosen AND `score_arms.explain_ok` passes.
- **controls**, on every run: score_correct's positive and negative controls; the selection grader
  must read each item's own record as right (480/480), another item's as wrong (477/477), and Form C
  as no choice.

## Result

| item set | n | top-ranked record right | shipped A155: chosen right / right end to end | **A156: chosen right / right end to end** |
|---|---|---|---|---|
| values given, symbol asked (`answer_0`) | 120 | 71 (59.2%) | 22 / 21 (17.5%) | **62 / 59 (49.2%)** |
| symbol only, "What was F?" (`answer_s`) | 120 | 34 (28.3%) | 0 / 0 (0.0%) | **28 / 27 (22.5%)** |
| quantity named in words (`answer_w`) | 120 | 106 (88.3%) | 34 / 32 (26.7%) | **93 / 89 (74.2%)** |
| one irrelevant value added (`answer_x`) | 120 | 63 (52.5%) | 13 / 9 (7.5%) | 13 / 9 (7.5%) |
| a value withheld, should decline (`d1`) | 120 | | 120 declined (100.0%) | 119 (99.2%) |
| no values, should decline (`d1_zero`) | 102 | | 102 (100.0%) | 102 (100.0%) |
| explain (`explain`) | 87 | 56 (64.4%) | 39 / 37 (42.5%) | 39 / 37 (42.5%) |
| out of scope, should decline (2,000 `d3_stems`) | 2,000 | | 1,982 (99.1%) | 1,982 (99.1%) |

Files: `results/endtoend_g88p_a155.json` (before), `results/endtoend_g88p_a156.json` (after).

## Where the losses were

**The ranking was mostly right; the confidence test threw it away.** On questions naming the
quantity in words, the top-ranked record was right 88.3% of the time and the confidence test let
28.3% through. `ask_qcover` asks what fraction of the question's words the record's name explains,
and it was tuned on lookups, textbook word problems and out-of-scope questions -- never on a
question carrying values. Every framing word counts against it: "What is output work? Take d_o =
82.5, F_o = 75.6." ranks the right record and scores 66% against a bar of 75.

**A156 treats the values as evidence** (`src/store/askparse.c`, `givens_bind_uniquely`): if the
question supplies exactly the inputs of the top-ranked record, the record computes one unknown from
exactly what was typed. It stays silent when the same givens bind another relation (series and
parallel resistance), and when the question names a different store variable ("P = 464, A =
3.39e-05. Compute v_d." binds I = P/A). Each clause has a test in `test_autopick` and a mutation
control in `gate_controls.py`, all caught. Wrong choices rose on one arm by one item (`d1`, 5 to 6:
the intensity case below) and on no other; out-of-scope
confidence is unchanged (1.60% of the first 2,000 stems, 2.01% of the other 14,989), because
out-of-scope questions carry no assignments.

## What the errors are

- **Wrong answers after the right choice are copying errors**, the failure s7 of
  `RESULT_CORRECTNESS.md` already documents: 2430 copied as 243.0, 272 as 263.0, 9.83 as 8.875.
- **One wrong choice is an ambiguous question**: "R_2 = 44.1, R_1 = 28.27, what was equivalent
  resistance?" fits series and parallel. Coverage (not A156) chose parallel and the model computed it.
- **The one `d1` miss is arguably right**: "r = 30, P = 5.33, what is I?" -- the item withholds A for
  I = P/A, and A156 chose I = P/(4*pi*r^2), which r and P do answer.
- **The 18 out-of-scope non-declines** are 10 glossary definitions, 7 explanations of a relation and
  1 tool call. None states a computed answer to the question asked except the one tool call.

## What remains, and what it does not claim

- **A spare value blocks A156** (every given must be a variable of the record), so `answer_x` is
  unchanged at 7.5%. Relaxing that is where the next wrong choices would come from; not done.
- **Explanations** carry no values, so only coverage applies: 42.5%.
- **The evaluation questions use the store's own symbols by construction.** A156 fires only when the
  student's symbols are the store's, so on a student who writes "vi" for v_0 the gain is smaller. On
  200 textbook questions (DEV) it never fires: textbooks state values in prose. The end-to-end
  figures describe questions typed the way the app's keypad flow suggests ("Given d = 150, t = 12,
  find the speed"), not arbitrary student phrasing.
- **The out-of-scope refusal is in-distribution for the model** (the D3 training class draws on the
  same pool of stems); the selection stage is not trained on them.

## Found on the way: three test suites that nothing ran

`test_askparse`, `test_autopick` and `test_ansmatch` were in the Makefile's `TESTS_*` lists and in
no line of `run_gates.sh`: `make check` built them and executed nothing. `test_autopick` had been
FAILING since A125 (it asserted a picker the app no longer shows), and its "answered directly" half
passed whatever the app did, because `PICK_ON` is 0 on both paths now. It was rewritten to observe
the record id the app sends, all three are wired into the gate loop, and
`tools/eval/gate_tests_wired.py` fails whenever a built suite is not executed (comments do not count
as execution; its control deletes one suite's invocation and is caught).
