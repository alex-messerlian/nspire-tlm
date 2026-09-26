# The shipped app, end to end: the selector was the bottleneck, not the model

**Current (A157):** right end to end on 77.5% (values given, symbol asked), 95.8% (symbol only),
74.2% (named in words), 7.5% (a spare value); declines unchanged. From 17.5 / 0.0 / 26.7 / 7.5% at
A155. The A157 section below has the last step.

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

## A157: the asked symbol breaks a tie

Reported from the device after A156: "Given m = 2, a = 3, find F" and "Given V = 12, I = 2, find R"
still said "no matching relation". Both are ties A156 refuses on purpose -- m and a bind F = m*a
AND F_net = m*a; V and I bind R = V/I AND P = V*I -- and both questions say which one they mean.
`asked_symbol_pick` takes the bound relations whose LHS the question names; if they are one
relation it is chosen, whatever the ranking put first. Two named relations ("find R and P", or
series and parallel resistance both written R_eqv) is refused.

| item set | A156: chosen right / wrong, right end to end | **A157** |
|---|---|---|
| values given, symbol asked (`answer_0`) | 62 / 6, 49.2% | **96 / 3, 77.5%** |
| symbol only (`answer_s`) | 28 / 6, 22.5% | **118 / 0, 95.8%** |
| named in words (`answer_w`) | 93 / 2, 74.2% | 93 / 2, 74.2% |
| spare value, withheld value, explain, out of scope | | unchanged |

Wrong choices FELL, because a named symbol now overrides a wrong word-based guess. On all 16,989
out-of-scope stems confidence is unchanged at 1.96%, and on 200 textbook questions it never fires.

It also exposed a defect in A156, found by trying the obvious neighbour of the reported questions:
**"Given V = 12, R = 6, find I" picked P = V^2/R.** The guard exempted "I" everywhere, as an English
word, so it did not see that the question asks for current. "a", "A" and "I" now count as symbols
where they close a clause ("find I", "what is a?") and stay words where a word follows ("I have...",
"a car"). No count in any table moved; the case is now declined, which is right: no store relation
computes I from V and R.

Controls, all caught: the tie-break removed, its ambiguity refusal removed, the clause rule removed;
the A156 controls re-aimed where A157 now decides their old case (the rule itself at "What is output
work? Take d_o = 82.5, F_o = 75.6.", which names no symbol and covers 66%; uniqueness at series and
parallel R_eqv).

## Held out: the rules on 900 items they were not designed on

A156 and A157 were written while reading the arms' items, so their gains there are not held-out
estimates. `tools/eval/selection_holdout.py` draws 900 fresh items from the arms' own producers
(`answer_control.py`, `worded_control.py`) with seeds no arm uses, and runs the same app code linked
against each revision's askparse.c (`results/selection_holdout.json`). Selection right / wrong /
declined:

| fresh item set (300 each) | A155 | A156 | A157 | A157 right end to end |
|---|---|---|---|---|
| values given, symbol asked | 49 / 14 / 237 | 157 / 14 / 129 | 253 / 5 / 42 | 82.7% |
| named in words | 66 / 10 / 224 | 208 / 10 / 82 | 208 / 10 / 82 | 66.7% |
| symbol only | 2 / 26 / 272 | 82 / 26 / 192 | 295 / 0 / 5 | 95.0% |

Same direction and similar size; wrong choices never rise. This controls for tuning to the items,
not for the generator's template family or its use of the store's symbols, which the arms share.

## What the errors are

- **Wrong answers after the right choice are copying errors**, the failure s7 of
  `RESULT_CORRECTNESS.md` already documents: 2430 copied as 243.0, 272 as 263.0, 9.83 as 8.875, and
  at A157 300 as 140.0 and 28400000 as 26100000.
- **One wrong choice is an ambiguous question**: "R_2 = 44.1, R_1 = 28.27, what was equivalent
  resistance?" fits series and parallel. Coverage (not A156) chose parallel and the model computed it.
- **The one `d1` miss is arguably right**: "r = 30, P = 5.33, what is I?" -- the item withholds A for
  I = P/A, and A156 chose I = P/(4*pi*r^2), which r and P do answer.
- **The 18 out-of-scope non-declines** are 10 glossary definitions, 7 explanations of a relation and
  1 tool call. None states a computed answer to the question asked except the one tool call.

## What remains, and what it does not claim

- **A spare value blocks A156 and A157** (every given must be a variable of the record), so
  `answer_x` is unchanged at 7.5%. Relaxing that is where the next wrong choices would come from;
  not done.
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
