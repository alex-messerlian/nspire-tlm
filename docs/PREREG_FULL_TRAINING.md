# Full-length training of the shipped configuration (pre-registered 2026-09-27, before any step ran)

**Why.** The shipped model (`train/ship.pt`) was trained for 8,000 steps at batch 24 and length 512:
98.3M tokens, about 9 per parameter, 45% of the roughly 20 that Hoffmann et al. find compute-optimal.
8,000 steps was the budget of the first runs, kept fixed so that every later run could be compared
step for step (`PREREG_TRAINING_RUN.md`: "unchanged from the d288 runs so the curves are comparable"),
and `RESULT_CAPABILITY_CLIFF.md` recorded the 45%. The author's plan was to train the final
configuration fully once it had been measured on the calculator. That run was never made; this is it.

**Configuration.** Identical to the shipped model except the number of steps: `DIM=352 LAYERS=6
HEADS=8 SEQ=512 BS=24 LR=3e-4` (AdamW, 3% warm-up, cosine to 10%, weight decay 0.1), `SEED=1`,
vocabulary 4,096, the current corpus (`corpus_sha acdc7b70cef192b7`, tokenizer `c60e1250c64df27e`,
41,361,954 training tokens: the corpus the Appendix A width comparison used).

**Budget.** `STEPS=17750`: 218.1M tokens, 20.0 per parameter, about 5.3 passes over the training
tokens. Held-out loss on every whole window of `train/mix4096_val.bin` every 1,000 steps
(`VAL_EVERY=1000`), a checkpoint every 2,000 (`CKPT_EVERY=2000`). A 600-step smoke test with the same
switches runs first.

**What the held-out curve decides (fixed now):**

1. Still falling over the last 4,000 steps by more than its step-to-step noise: longer training still
   helps, and a run of twice this budget is justified, because the calculator fixes the size and the
   length of training is what remains.
2. Rising over the last 4,000 steps: repeated data has started to cost more than it gives. The best
   checkpoint by held-out loss is examined, and more unique data, not more steps, is the next lever.

**What decides whether it replaces the shipped model (fixed now).** Scored on the calculator's decoder
(int8, greedy, `tools/eval/score_on_device_decoder.sh` and `tools/eval/score_endtoend.py`), by the same
scripts that produced the paper's numbers: it ships if it is at least as good on every set that should
be declined and better on answer accuracy. Otherwise the shipped model stays, and the comparison is
reported in the paper as a result.

**If it ships, these are measured again:** every quality number in the paper (FACTS.md sections 8 and
8b), the width comparison's 10.9M row stays as it is (it is a fixed-steps comparison and says so), the
calculator parity check and Figure 1's logged turn on the device, the app screens (`make_screens.py`),
and Appendix B, whose video shows the current model. Speed, memory, the cost model and the frontier
depend on the size and the engine, which do not change; one decode run on the device confirms it.

**How the rule is read (added 2026-09-28, 02:10 PDT, while the run was at step 14,400 and before
any score of the new model existed).** `tools/eval/compare_full_training.py` applies it, and its
controls were run first: the shipped model against itself does not ship, one more correct answer in
both totals ships, and one fewer decline blocks it.

- *Every set that should be declined*, counted in items declined, new at least equal: with the
  relation supplied, value withheld, no values and relation does not apply; end to end with the
  shipped selector, the same three and the 2,000 out-of-scope questions.
- *Better on answer accuracy*: correct answers summed over the four answerable sets, strictly more
  in both totals, with the relation supplied (480 items) and end to end with the shipped selector
  (480 development plus 1,200 fresh items).

**Correction (2026-09-28).** The training file holds **41,578,062** tokens, not the 41,361,954 given
under Configuration, which has no source in the repository: `train/mix4096_train.bin` is 83,156,124
bytes of uint16, and replaying `train/prepare.py`'s selection on this corpus reproduces it exactly
(35,694,988 synthetic tokens from 316,220 documents, plus 6,303,055 textbook tokens from 1,850 of
the 2,987 modules, 15.0% of the mixture; 1% held out, 419,981 tokens). The budget is therefore about
**5.2** passes over the training tokens, not 5.3. Nothing else in the run depends on the figure.

## Outcome (2026-09-28, 02:45 PDT): the rule is NOT met

The run finished normally: 17,750 steps in 156 minutes, `train/full_d352.pt` (`corpus_sha
acdc7b70cef192b7`, `tok_sha c60e1250c64df27e`, 10,908,128 parameters). Mean training loss over the
last full 500-step window **0.6969**; held-out loss (all 820 windows of `train/mix4096_val.bin`)
**0.7129** at the end, 0.7218 / 0.7183 / 0.7150 / 0.7141 / 0.7120 at steps 13,000 to 17,000.

**The curve rule (informational).** Held-out loss fell from 0.7218 at step 13,000 to 0.7120 at
17,000, 0.0098, more than any change between consecutive measurements (0.0009 to 0.0035), so by
rule 1 a longer run is justified. It was flat over the final 750 steps (+0.0009), where the cosine
schedule ends.

**The ship rule**, applied by `tools/eval/compare_full_training.py` (full output in
`results/compare_full_training.txt`):

| | 8,000 steps (shipped) | 17,750 steps |
|---|---|---|
| declines, relation supplied: value withheld / no values / does not apply | 120 / 102 / 0 | **118 / 98** / 4 |
| declines, end to end: value withheld / no values / does not apply / out of scope | 119 / 102 / 119 / 1,982 | **118** / 102 / **118** / **1,980** |
| correct answers, relation supplied (480 items) | 426 | **436** |
| correct answers, end to end (1,680 items) | 1,062 | **1,074** |

Five of the seven decline measures are worse, by one to four items, so **by the rule fixed above the
8,000-step model stays**. The answer side passes both totals; strict accuracy rises from 385 to 427 of
the 480 answerable items, and incorrect answers end to end fall from 15 to 12 (development) and from
42 to 34 (fresh).

**What the six new failures are** (relation supplied; the 8,000-step model declined all six): three
tool calls that should not exist (an `integ` of a made-up expression using `d_i = 2.3` as the spring
constant; an `integ` and a `solve` over Planck's constant) and three explanations of the relation in
place of a refusal. The four from the no-values set each give only a physical constant (Planck's
`h` three times, `R` once). Its four new declines on the relation-does-not-apply set are correct
refusals.

**The decision is the author's.** The author asked for a fully trained model and wrote "fully
trained" into the paper before this result existed. Keeping the rule's outcome means that wording
changes; shipping the 17,750-step model means overriding the rule stated here, which must then be
reported with this table.
