# Pre-registration: the quality axis of the frontier

Written **before** the runs, 2026-09-21. `docs/RESULT_FRONTIER_BATTERY.md` measures throughput
across widths and says plainly that the quality axis has one point. This fills it in.

## The question

The frontier's throughput axis is measured at d176, d264 and d352. Every trained checkpoint is
d352, so "10.9 M parameters at >= 2 tok/s" currently means "the largest model that runs", not
"the smallest model that works". **Where does the tool-augmented architecture stop working as
the model shrinks?**

This matters beyond bookkeeping. The paper's thesis is that capability here comes from the
system -- a retrieved record to read and a calculator to call -- rather than from parameters.
If that is true, grounded computation should degrade *slowly* with width, because the hard parts
were never in the weights. If `d1` collapses at d176, the thesis is weaker than claimed and the
paper should say so.

## Design

**Varied:** `DIM` in {176, 264, 352}.

**AMENDED BEFORE ANY LADDER RUN, and the amendment is the point of having run a smoke test.**
The first design reused the shipped d352 checkpoint as the third point. It cannot be reused: the
shipped model carries `corpus_sha 24c64532f724c6c8` and the corpus on disk is now
`acdc7b70cef192b7` -- regenerated earlier today. The relation coverage is unchanged (1,619 heads
both sides), so the two corpora are the same *experiment*, but they are not the same *documents*.

Comparing a new d176 against that d352 would move width and corpus together and would measure
neither. This is the stratum-drift failure recorded in the project log, where a harness inferred its
labels from a generator that had moved and the headline did not budge because the headline did
not depend on the labels. Here the cost of getting it wrong is an entire figure in the paper.

So **d352 is retrained on the current corpus** as part of the ladder. The shipped model is
untouched and remains the d352 trained on `24c64532f724c6c8`; the ladder is a separate,
internally consistent set of three. Every number in the ladder figure will carry
`corpus_sha acdc7b70cef192b7`, and the ladder's d352 is NOT the shipped model's score.

**Held constant, all of it:** `LAYERS=6 HEADS=8 SEQ=512 BS=24 LR=3e-4 STEPS=8000 SEED=1`,
`corpus_sha 24c64532f724c6c8`, `tok_sha c60e1250c64df27e`. Both dims divide 8, so the head count
does not move. `hidden_dim` follows the same 8/3-rounded-to-256 rule at every width (512 / 768 /
1024), which is the rule `tools/make_shape.py` used for the throughput sweep -- so the trained
and timed shapes are the same shapes.

**A stated confound, not a hidden one.** Holding `STEPS` constant holds the training BUDGET
constant, not convergence. A smaller model may be nearer or further from its own optimum at 8,000
steps than d352 is. This is the right control for "same budget, less width" and the wrong one for
"each width at its best". Final loss is reported per width so the reader can see which regime we
are in, and no claim of the form "width w is intrinsically worse" will be made from one budget.

## Predictions, with bands

Bands are set now. A miss is a finding to explain, not a number to retune.

| arm | d352 (measured) | d264 predicted | d176 predicted |
|---|---|---|---|
| `d1` grounded compute | 100.0% | 85-100% | 55-95% |
| `d1_zero` | 100.0% | 85-100% | 50-95% |
| `refuse` | 100.0% | 85-100% | 60-100% |
| `explain` in-distribution | 83.5% | 55-80% | 25-65% |
| `fit` | 1.1% | 0-10% | 0-10% |
| final loss | 0.7960 (shipped, different corpus) | 0.83-0.95 | 0.95-1.20 |

The d352 row above is the SHIPPED model's score on the OLD corpus and is shown only as the prior
that set these bands. The ladder's own d352 will be measured on the current corpus and is the
number the figure uses.

The `d1` bands are deliberately wide because **nobody has measured this and a narrow band would
be invented**. The interesting outcomes are the two edges:

- **d176 `d1` >= 95%** would be the paper's strongest single result: grounded computation is
  carried by the architecture down to ~3 M parameters, where the model is far too small to hold
  the physics it is using.
- **d176 `d1` <= 55%** locates a capability cliff, which is also publishable and would change
  the abstract from "capability without scale" to "capability with *much less* scale, down to a
  measured floor".

`fit` is predicted to stay near zero at every width because it is already near zero at d352; it
is a corpus/supervision failure, not a capacity one, and if it were to *improve* at smaller
width that would indicate something wrong with the arm rather than a real gain.

## Decision rule

The width ladder goes in the paper whatever it shows. Nothing here selects a checkpoint to ship:
**the shipped model is unchanged at d352** and these runs are measurements, not candidates. Said
explicitly so that a good d264 result cannot quietly become a shipping decision after the fact.

## Commands

    RUN=w176 SEED=1 DIM=176 LAYERS=6 HEADS=8 SEQ=512 BS=24 STEPS=8000 .venv-tok/bin/python train/select_run.py
    RUN=w264 SEED=1 DIM=264 LAYERS=6 HEADS=8 SEQ=512 BS=24 STEPS=8000 .venv-tok/bin/python train/select_run.py
    RUN=w352 SEED=1 DIM=352 LAYERS=6 HEADS=8 SEQ=512 BS=24 STEPS=8000 .venv-tok/bin/python train/select_run.py

Then `score_arms` against each, with `TOK=` pinned to the tokenizer they were trained with.


## Amendment 2 (2026-09-24, before the runs): a second training seed

Seed 1 found `d1` -2.5 pp and `explain` -69.3 pp from d352 to d176. That headline rests on ONE
training run per width, and the first question a reviewer will ask is whether it is seed noise.

**Added:** `SEED=2` at both widths. Everything else identical to seed 1, and verified before launch:
`corpus_sha acdc7b70cef192b7`, `tok_sha c60e1250c64df27e`.

**What would change the conclusion, stated now:** the dissociation stands if, at seed 2, d176's
`d1` drop stays under 10 pp AND its `explain` drop stays over 40 pp. If either fails, the paper
reports the two seeds side by side and says the effect is seed-sensitive -- it does not average
them into a result that neither seed showed.

    RUN=w176s2 SEED=2 DIM=176 LAYERS=6 HEADS=8 SEQ=512 BS=24 STEPS=8000 .venv-tok/bin/python train/select_run.py
    RUN=w352s2 SEED=2 DIM=352 LAYERS=6 HEADS=8 SEQ=512 BS=24 STEPS=8000 .venv-tok/bin/python train/select_run.py
