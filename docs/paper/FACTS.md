# Consolidated facts sheet

Every number the paper will cite, with its source and its caveat. **A number without its
conditions is not a number** -- so conditions are attached here rather than remembered.

Compiled 2026-09-22.

---

## 1. The shipped model

| | |
|---|---|
| architecture | Llama-2 style decoder transformer |
| dim / layers / heads / kv-heads | 352 / 6 / 8 / 8 |
| hidden_dim | 1024 (8/3 rule, rounded to multiple of 256) |
| context | 512 |
| vocabulary | 4,096 |
| **parameters** | **10,903,552** in the weight matrices (embedding shared with the classifier, counted once); **10,908,128** including the 4,576 RMSNorm weights (13 norms x 352). Both are 10.9M. Counted from `train/ship.pt` on 2026-09-28 |
| quantisation | int8 Q8_0, **group 88**, shared classifier; hidden **padded 1,024 -> 1,056 with zeros** in the file (A153) so 88 divides every row |
| **checkpoint size** | **11,629,696 B = 11.09 MiB** (the defective unpadded file was 11,417,728 B) |
| training | 8,000 steps, seed 1, BS 24, LR 3e-4, seq 512 |
| final loss | 0.7960 |
| `corpus_sha` | `24c64532f724c6c8` |
| `tok_sha` | `c60e1250c64df27e` |
| file | `train/ship.pt` (= `train/archive_a112/math1.pt`) |

**Group 88 is not a free choice.** `FIXED_GS` is compile-time in `src/runq_nspire.c` so the hot
loop sheds two `__divsi3` per group. Every ROW length (dim, hidden) must be a multiple of it (A151:
the engine ignores a row's remainder; rq_probe now refuses a violating file). dim must be a
multiple of 88; the hidden width is zero-padded to one (exact: PyTorch max |logit diff| 3.5e-6,
argmax identical). RoPE forces an even head size; with heads=8 that admits **176, 352, 528**.
A uniform group of 32 also fixes the rows and was measured 27% slower (results/device_g32/).

## 2. Tokenizer

4,096 entries, trained on this corpus. **11 special tokens**, ids 0-10, assigned first so they
survive a retrain:

    <unk> <q> </q> <r> <a> <tool> <arg> </tool> <res> </res> <end>

A domain vocabulary rather than a 32,000-entry general one is a **throughput** decision: the
output projection is `dim x vocab` and is read every token. Even at 4,096 the classifier is 10% of
per-token cost.

**Hazard, recorded because it has bitten:** `train/prepare.py` retrains the tokenizer, and a
checkpoint paired with the wrong one scores ~0 rather than erroring. Between two consecutive runs
3,886 of 4,096 ids changed. Checkpoints stamp `tok_sha`; `score_arms` aborts on mismatch.

## 3. Corpus and store

| | |
|---|---|
| documents | **316,220** (`wc -l` says 316,219: the file's last line has no newline; `train/corpus_check.py` parses 316,220) |
| compute documents | 239,750 (bands are checked against this, not the total) |
| store records (shipped) | **177** after cleaning |
| retrieval candidates incl. knowledge tier | 1,619 |
| composition | answer 59%, K1 8%, D1 7%, K3 5%, D2 4%, K2 4%, R1 3%, EXPLAIN 3%, C1 3%, D3 2%, C2 1%, C3 1% |
| knowledge tier | 52,390 docs (16.6%) |
| symbolic-tool tier | 24,080 docs (7.6%) |
| audited ill-posed rate | **3.00% [1.72, 5.17], n = 400**, from an EARLIER 29,982-document revision, read by two AI passes and adjudicated by AI (no person); false negatives are not controlled; predates later fixes |

**The 3.00% is a LOWER BOUND and must never be quoted bare** -- two readers missing the same defect
produces a smaller number, not a warning. `gate_shipping_number.py` fails any citation without the
n and the interval. ASSUMPTION BEHIND "LOWER BOUND": that the 12 flags are real
defects. They were adjudicated by AI, not verified by a person, so neither the false-positive nor the
false-negative rate is measured; the paper therefore says "the interval reflects sampling
uncertainty only" and does not call the figure a bound.

## 4. Throughput -- battery, USB disconnected, core 396 MHz, SHIPPED padded group-88 engine

Source: results/device_g88p/ via `tools/paper/cost_model.py results/device_g88p`.

| | |
|---|---|
| **decode** | **1.882 tok/s**, 35 tokens at positions 53-87, one run (instrumented: forward-only 18.570 s of 18.596) |
| prefill | 53 tokens in 24.294 s |
| whole turns (8, A152) | **26-75 s per answer, first output after 15.5-40.1 s** |

Earlier, on the DEFECTIVE unpadded engine: 1.921 tok/s (three runs, 1.920-1.923), prefill 23.8 s.
Group 32: 1.568 tok/s, prefill 29.9 s.

Throughput by position, filled-cache model: 2.417 tok/s at 0 (extrapolated), 1.600 at 128, 1.196 at
256, 0.810 over the last window (mean 497.5), 0.795 at 511 (extrapolated) -- a 67% drop.

## 5. Cost model -- shipped engine, ONE block, windows at their mean positions

| | |
|---|---|
| depth (positions 3-14) | 46,102 + 63,777 L us, R^2 1.000000 |
| width (positions 11-18, last sweep block, 3 widths) | 32,338 + 37,474 N us, N in millions of parameters, R^2 0.99983 |
| position, filled cache | 413,753 + 1,650.9 p us, R^2 0.999992 (intercept extrapolated) |
| position, empty cache | 420,282 + 1,182.3 p us; filled slope +39.6% |
| stages (positions 3-14), share of 428,833 us | ffn 56.7%, qkv 16.5%, classifier 10.6%, attention 9.0%, RoPE 5.0%; matmul 83.9%; unattributed 0.28% |
| consistency, same block | fixed vs depth intercept 0.15%; per-layer vs slope 0.31%; all stages vs position model at p = 8.5 0.03% |
| decode prediction | -0.4% (35 tokens), -0.2% on forward-only time; empty-cache model -5.3%. One prompt, decode only. |
| group-size cost (same engine code) | per token at L6: 419.1 ms defective group 88, **428.9 ms padded group 88 (+2.3%)**, 534.0 ms group 32 (+27.4%) |

## 6. Memory

| | |
|---|---|
| heap ceiling, right after a reset | **22,576,128 B = 21.53 MiB** (also measured 21.64 after another reset) |
| heap ceiling after a crashed load | **5.02 MiB** -- does not recover until the calculator is reset |
| d352 footprint, shipped padded file | **19.34 MiB** = 11.09 (checkpoint) + 8.25 (fp32 KV cache) |
| two-block limit | **at least 22.10 MiB**: d384 at group 32 (13.10 + 9.00 MiB) LOADED (results/device_g32/); d440 (26.87 MiB, and 27.19 padded) does not fit |
| fragmentation across 4 load/free cycles | **none**, 22,576,128 B unchanged to the byte |

**The binding constraint is the SUM, not the checkpoint.** KV cache is `2 x L x seq x dim x 4`
bytes -- 43% of the d352 footprint.
What was measured: d352 (10.9M) loads; d384 (12.2M, group 32) loads at 22.10 MiB total; d440 (16.6M)
does not fit, although its checkpoint alone is under the largest single allocation. For six
layers, context 512 and an fp32 cache, the limit lies between 12.2M and 16.6M parameters, and the
two-block total between 22.10 and 26.87 MiB. Because the KV term is linear in `seq_len`, context length and parameter count trade
directly against each other.

**Demo consequence: reset before demonstrating.** ChatTLM needs 19.34 MiB; a device that has run
something that crashed, or a program that exits without freeing (golden_dev before A154), has less.

## 7. Frontier -- shipped padded group-88 engine, FILLED KV cache (results/device_g88p/)

Tokens/s averaged over positions 11-18 and 259-266 (bench_sweep: WARM 3, REPS 8). Random weights
except the trained d352; hidden widths padded to multiples of 88 like the shipped file.

| dim | params (unpadded) | file + KV | 11-18 | 259-266 |
|---|---|---|---|---|
| 176 | 3,086,336 | 7.26 MiB | 6.815 | 2.715 |
| 264 (6 heads) | 6,403,584 | 12.70 MiB | 3.643 | 1.815 |
| **352 (trained)** | **10,903,552** | **19.34 MiB** | **2.273** | 1.180 |
| 352 (random, seed 99) | | | 2.246 | 1.228 |
| 440 (10 heads) | 16,586,240 | 27.19 MiB | **does not fit** | |

Largest model at >= 2 tok/s: 10.9M over positions 11-18; 6.4M at position 128 (interpolated); 3.1M
over 259-266. Trained vs random d352: 1.2% at 11-18, 3.9% at 259-266 (random faster at long context:
software floating point is data-dependent).

GROUP-32 ABLATION (results/device_g32/, same engine code, finer widths): d192 5.24 / 2.37, d256 3.12
/ 1.65, d320 2.05 / 1.21, d352 trained 1.85 / 1.05, d384 1.65 / 1.00 tok/s -- ~27% slower per token
than the padded group-88 layout, and d384 LOADS (22.10 MiB).

## 8. Quality -- shipped model, ON THE CALCULATOR'S DECODER, the RIGHT RELATION SUPPLIED

**Conditional on the right relation.** Every number in this section hands the model the item's own
record (`build/devasm` takes the record id), which is what the app did while the student picked from
a list. Since A125 the app picks the relation itself; what the student actually gets is s8b.

Prompts from the device's own `ask_build` + `ns_assemble` (`build/devasm`); generation by the int8
engine through the app's own loop (`src/store/gencore.c` via `build/int8gen`), greedy; the device's
own `answer_states_result` for the prose. One pass (greedy is deterministic). History of how these
replaced the earlier figures: `docs/RESULT_CORRECTNESS.md`.

| item set (arm) | n (relations) | **padded group 88, as shipped (A153)** | group 88 before A153 (defect) |
|---|---|---|---|
| values given (`answer_0`), right | 120 (88) | **96.7%** | 91.7% |
| same, strict (correct rounding; trailing-zero fix applied) | | 89.2% | 79.2% |
| quantity named by symbol (`answer_s`) | 120 (82) | 97.5% | 95.8% |
| quantity named in words (`answer_w`) | 120 (75) | 95.8% | 89.2% |
| one irrelevant value added (`answer_x`) | 120 (84) | **65.0%** | 57.5% |
| decline: a needed value withheld (`d1`) | 120 (87) | 100.0% | 99.2% |
| decline: no values given (`d1_zero`) | 102 (74) | 100.0% | 99.0% |
| decline: record does not fit, all bound (`fit`) | 120 (79) | **0.0%** | 0.0% |
| explain, form only (`explain`) | 87 (71) | 93.1% | 85.1% |
| **deterministic baseline, no model** (same prompts, graders) | | answer 100/100/100/99.2; decline 100/100; fit 0; explain format 78.2% | |
| **paired distractor** on `answer_0`: spare LAST / FIRST | 120 | 113 / **61** (base 116) | 104 / 55 (base 110) |

Sources: `results/correct_int8_g88p_ship.json`, `results/arms_int8_g88p_ship.json`,
`results/paired_distractor_g88p_ship.json`. Padding is exact for the model it pads (PyTorch max
|logit difference| 3.5e-6, argmax identical). It is NOT the same quantisation as group 16, which uses
finer groups, and the two differ by up to 1.7 points on some rows (`answer_x` 65.0 against 66.7,
`answer_w` 95.8 against 96.7); they agree on the paired distractor (116 / 113 / 61).

The paired result is the distractor finding. `answer_x` (65.0%) is a separate set whose spare's
position is SHUFFLED (answer_control.py): first in 47 of 120, middle in 32, last in 41. Split by that
position (`tools/eval/spare_position.py`, `results/spare_position_g88p_ship.txt`): **last 41/41
(100.0%), first 24/47 (51.1%), middle 13/32 (40.6%)** -- the positional finding on independent
items (group 16: 41/41, 25/47, 14/32). CORRECTED: this sheet used to say answer_x "places its spare
first"; it does not. Details: `docs/RESULT_CORRECTNESS.md` s7.

**Binding is by name, not position** (`tools/eval/paired_reorder.py`, `results/paired_reorder_g88p_ship.json`):
the 91 `answer_0` items with two or more givens, relevant values reversed and nothing inserted: 87/91
right before and after (2 lost, 2 gained). The script reproduces the earlier ad hoc group-16 run
exactly (87/91 -> 87/91) before it is used on the shipped engine.

**Declines name the missing value** (`tools/eval/refusal_names.py` on the saved generations): `d1`
103 of 120 declines (85.8%) name only variables the prompt's `missing:` field lists; the rest name a
given one ("d_i is not given" under missing:d_o) or decline in another form. `d1_zero` 102/102. The
script reproduces the earlier ad hoc figures (group 32 104/120, defective group 88 98/119) first.

"Right" = the last tool result within 0.1% of a reference computed by evalcli from the record and
the question's values, AND the prose states it within 2% (the device's own check). Strict replaces
the 2% with "a correct rounding at the precision shown", which rejects truncation (4737.6 as "4737")
and one correct rounding (16667.89 as "16670", trailing zero counted as significant).

`answer_0` errors on the shipped engine (4 of 120): all 4 are calls with a value mis-copied (2430 ->
243.0, 272 -> 263.0). `answer_x`: 42 of 120 not right, 35.0% of items a wrong call.

**The group-88 defect is fixed** (A153, commit `edcb002`): `quantize`/`matmul` skipped the remainder
of every row whose length the group did not divide -- the FFN down-projection, hidden 1024, 56 inputs
ignored per layer. The hidden width is padded with zeros to 1,056 = 12 x 88. Held by `rq_probe`'s
row check (A151, refuses a file whose group does not divide a row) and `test_ckpt`'s rowalign case.

**Unreachable on the device** (records not in the shipped store): `refuse`, `fit_ho`, `explain_ho`.
They are no longer quoted.

Device demo transcript 6/6 and fabrication 0.6% (`RESULT_A112_BAND`) are from the older fp32 harness
and are not re-measured on this decoder.

## 8b. End to end -- the APP chooses the relation, then the calculator's decoder answers

`tools/eval/score_endtoend.py`: selection by app.c's own `open_picker()` (`build/autoasm`, not a
copy), then `build/int8gen` on the shipped file. Same items as s8. Definitions: "right" as in s8,
against the item's reference; a decline arm is right when the output declines; `explain` is right
only if the right relation was chosen AND the format grader passes. Full account and before/after:
`docs/RESULT_ENDTOEND.md`.

| item set | n | right relation chosen | **right, end to end** | declined | wrong |
|---|---|---|---|---|---|
| values given (`answer_0`) | 120 | 96 (80.0%) | **93 (77.5%)** | 23 | 4 |
| quantity named by symbol only (`answer_s`) | 120 | 118 (98.3%) | **115 (95.8%)** | 2 | 3 |
| quantity named in words (`answer_w`) | 120 | 93 (77.5%) | **89 (74.2%)** | 27 | 4 |
| one irrelevant value added (`answer_x`) | 120 | 13 (10.8%) | **9 (7.5%)** | 107 | 4 |
| a needed value withheld (`d1`), should decline | 120 | -- | **119 (99.2%)** | | 1 |
| no values given (`d1_zero`), should decline | 102 | -- | **102 (100.0%)** | | 0 |
| explain | 87 | 39 (44.8%) | **37 (42.5%)** | 47 | 3 |
| out of scope (`d3_stems`, first 2,000), should decline | 2,000 | -- | **1,982 (99.1%)** | | 18 |

Shipped app A157 (`results/endtoend_g88p_a157.json`). The answer rows through the three versions of
the selector, right end to end (`answer_0` / `answer_s` / `answer_w` / `answer_x`):

| selector | files | right end to end |
|---|---|---|
| A155: word coverage only | `endtoend_g88p_a155.json` | 17.5 / 0.0 / 26.7 / 7.5% |
| A156: + the givens bind one relation | `endtoend_g88p_a156.json` | 49.2 / 22.5 / 74.2 / 7.5% |
| A157: + the asked symbol breaks a tie | `endtoend_g88p_a157.json` | **77.5 / 95.8 / 74.2 / 7.5%** |

Decline and out-of-scope rows are the same in all three except `d1` (100.0 -> 99.2% at A156, below).

**Complete accounting (A158)** -- right / wrong / declined; for sets
that should decline, declined / answered. Sources: `results/endtoend_g88p_a155|a156|a157.json`,
`results/selection_holdout.json` (fresh families AND the `fit` negative set under all three
selectors), `results/deterministic_e2e.json` (the no-model path behind the A157 selector).

| item set | n | A155 | A156 | A157 | no model (A157 selector) |
|---|---|---|---|---|---|
| values given | 120 | 21/2/97 | 59/4/57 | 93/4/23 | 96/1/23 |
| symbol only | 120 | 0/0/120 | 27/1/92 | 115/3/2 | 118/0/2 |
| named in words | 120 | 32/2/86 | 89/4/27 | 89/4/27 | 93/2/25 |
| one irrelevant value | 120 | 9/4/107 | 9/4/107 | 9/4/107 | 13/2/105 |
| explain | 87 | 37/3/47 | 37/3/47 | 37/3/47 | 26/3/58 |
| withheld value (decline) | 120 | 120/0 | 119/1 | 119/1 | 116/4 |
| no values (decline) | 102 | 102/0 | 102/0 | 102/0 | 93/9 |
| values fit an INAPPLICABLE relation (`fit`, decline) | 120 | 120/0 | 120/0 | 119/1 | 118/2 |
| out of scope (decline) | 2,000 | 1982/18 | 1982/18 | 1982/18 | 1981/19 |
| FRESH values given | 300 | 48/2/250 | 153/5/142 | 248/5/47 | 253/2/45 |
| FRESH symbol only | 300 | 2/3/295 | 79/6/215 | 285/10/5 | 295/0/5 |
| FRESH named in words | 300 | 63/4/233 | 200/9/91 | 200/9/91 | 209/2/89 |
| FRESH one irrelevant value | 300 | 23/18/259 | 23/18/259 | 23/18/259 | 40/8/252 |

- Wrong ANSWERS rise with coverage: dev answerable sets 8 -> 13 -> 15 while right rises 62 -> 184 ->
  306 and declines fall 410 -> 283 -> 159. At A157: 8 wrong CHOICES, 7 then declined, 1 wrong answer.
- `fit`: no selector version ever chose the inapplicable relation (0/120 each); A157's one answer
  ("Estimate omega ... v = 24.7, r = 1.74") used omega = v/r, which those values DO answer.
- The no-model path is right more often and wrong less often on EVERY answerable set, dev and fresh;
  the MODEL declines more reliably (withheld 119 vs 116, no values 102 vs 93) and passes the
  explanation check more often (37 vs 26). The 13 no-model non-declines on withheld/no-values: the
  selector chose a GLOSSARY TERM (e.g. "null measurements" for "Measurements give ...", the
  "equivalent resistance" entry for "compute the equivalent resistance") and the no-model path shows
  the definition where the model declines.
- Selection counts on the fresh families, right / wrong / none: values given 49/14/237 -> 157/14/129
  -> 253/5/42; words 66/10/224 -> 208/10/82 (unchanged at A157); symbol 2/26/272 -> 82/26/192 ->
  295/0/5; irrelevant value 41/15/244 at all three.
- Scope: fresh items control for tuning to particular items, NOT the generator's templates or its
  use of the store's own symbols. The out-of-scope pool is also the D3 training pool
  (in-distribution refusal), and its questions assign no values, so it cannot test A156/A157.

Readings, each checked on the items:
- Wrong answers are copying errors on the right relation (2430 -> 243.0, 300 -> 140.0, 414.8 -> 414.0),
  except one: "R_1, R_2, what was equivalent resistance?" is ambiguous (series or parallel); word
  coverage chose parallel and the model computed it.
- The one `d1` "wrong" is "r = 30, P = 5.33, what is I?": the app chose I = P/(4*pi*r^2), which the
  givens do answer. Counted as wrong because the item's label says decline.
- The 18 out-of-scope non-declines: 10 glossary definitions, 7 explanations of a relation, 1 with a
  tool call. The model's refusal here is in-distribution (the D3 training class draws on this pool);
  the SELECTION is not trained.
- Scope: the evaluation questions use the store's own symbols by construction, and A156/A157 fire
  only when the student's symbols are the store's -- A157's 95.8% on symbol-only questions is what a
  student gets who types the store's symbol for the thing asked. On 200 textbook questions (DEV)
  neither fires, because textbooks state values in prose.

**No-model path on the two decline sets (paper s7):** behind the A157 selector, `build/autoasm` chose a glossary term on **12 of the 222** items (3 of 120 value-withheld, 9 of 102 no-values) and a relation on 66; the no-model path's 13 answers there are those 12 definitions plus the I = P/(4 pi r^2) item both paths answer. Recounted 2026-09-28 from `corpus/split_d1.json` and `corpus/split_d1_zero.json`; the paper had said 13.

## 8c. The full-length run (17,750 steps) -- scored, and NOT shipped by the pre-registered rule

`docs/PREREG_FULL_TRAINING.md` (fixed before the run) and its Outcome section. `train/full_d352.pt`:
the shipped configuration and seed on the current corpus (`corpus_sha acdc7b70cef192b7`, `tok_sha
c60e1250c64df27e`), 17,750 steps at batch 24 x 512 = **218.1M tokens, 20.0 per parameter**, about
5.2 passes over the 41,578,062 training tokens; 156 min on the laptop GPU. Training loss (last full
500-step window) **0.6969**; held-out loss **0.7129** (820 windows), 0.7120 at step 17,000. Int8 file
`build/int8/full_d352_g88.bin`, 11,629,696 B, same layout as the shipped one.

Relation supplied, calculator's decoder (s8 definitions), 8,000-step -> 17,750-step:

| item set | device check | strict |
|---|---|---|
| values given | 116 -> 115 | 107 -> **113** |
| named by symbol | 117 -> 117 | 108 -> 115 |
| named in words | 115 -> **119** | 101 -> 116 |
| irrelevant value added | 78 -> **85** | 69 -> 83 |
| decline: value withheld | 120 -> **118** | |
| decline: no values | 102 -> **98** | |
| decline: relation does not apply | 0 -> 4 | |
| explain (format) | 81 -> 81 | |

End to end, shipped selector (A157), correct / incorrect / declined: values given 93/4/23 -> 91/6/23;
symbol 115/3/2 -> 115/3/2; words 89/4/27 -> 92/1/27; irrelevant value 9/4/107 -> 11/2/107; explain
37/3/47 unchanged; declines withheld 119 -> 118, no values 102 -> 102, does not apply 119 -> 118, out of
scope 1,982 -> 1,980. Fresh (A157): 248/5/47 -> 251/3/46, words 200/9/91 -> 204/5/91, symbol 285/10/5
-> 286/9/5, irrelevant 23/18/259 -> 24/17/259. Incorrect answers end to end 15 -> 12 (development),
42 -> 34 (fresh).

Other read-outs, 8,000-step -> 17,750-step: paired distractor base 116 -> 115, last 113 -> 113, FIRST
61 -> 60 (distractor value in the call in 43 of 59 -> 36 of 60 failures); reversing the values, of 91
before -> after, 87 -> 87 (8,000 steps) and 86 -> 87 (17,750); `answer_x` by position first 24/47 -> 28/47, middle 13/32 -> 16/32, last 41/41 -> 41/41;
declines naming only the missing variable 103/120 -> 107/118; group 16 against group 88 at most 1.7
-> at most 0.8 points; fp32 greedy strict on values given 94.2%, the same as int8 (113/120).

The six missing-value items it no longer declines: an `integ` of a made-up expression (`d_i = 2.3`
used as the spring constant), an `integ` and a `solve` over Planck's constant, and three explanations
in place of a refusal; the four from the no-values set each give only a physical constant (`h` three
times, `R` once). Sources:
`results/{correct,arms}_int8_g88p_full_d352.json`, `results/endtoend_g88p_full_d352_a15{5,6,7}.json`,
`results/selection_holdout_full_d352.json`, `results/paired_{distractor,reorder}_g88p_full_d352.json`,
`results/spare_position_g88p_full_d352.txt`, `results/refusal_names_g88p_full_d352.txt`,
`results/{correct,arms}_int8_g16_full_d352.json`, `results/correct_fp32_greedy_device_full_d352.json`,
`results/compare_full_training.txt`.

**Decision:** five of seven decline measures are worse, so by the rule the 8,000-step model stays.
Whether to ship the full-length model anyway is the author's decision (the paper says "fully
trained"); until then every other section of this sheet describes the 8,000-step model.

## 9. Width ladder -- corrected 2026-09-25, on the calculator's decoder at group 16

**The previous version of this section is retracted** (`docs/RESULT_CORRECTNESS.md`): its headline
"-2.5 pp on grounded computation" was the refusal arm `d1`, scored fp32 / T=0.8 / stale prompts.

Both widths on `corpus_sha acdc7b70cef192b7`, `tok_sha c60e1250c64df27e`, L6 H8, 8,000 steps, two
training seeds each. Scored by `tools/eval/score_on_device_decoder.sh 16` -- int8 at group 16 so the
row-alignment defect (which drops 72/512 hidden inputs at d176 vs 56/1024 at d352) does not confound
width. Table printed by `tools/paper/ladder_table.py 16`:

| item set | d176 s1 | d176 s2 | d352 s1 | d352 s2 | change of means |
|---|---|---|---|---|---|
| decline: value withheld | 99.2 | 100.0 | 99.2 | 100.0 | 0.0 |
| decline: no values | 100.0 | 100.0 | 100.0 | 100.0 | 0.0 |
| right: values given | 65.0 | 83.3 | 95.0 | 92.5 | **-19.6** |
| right: by symbol | 70.8 | 77.5 | 99.2 | 98.3 | -24.6 |
| right: in words | 75.0 | 80.8 | 96.7 | 95.8 | -18.3 |
| right: extra value | 41.7 | 55.8 | 63.3 | 52.5 | -9.2 |
| explain (form) | 64.4 | 52.9 | 93.1 | 93.1 | **-34.5** |
| judge fit | 0.0 | 0.0 | 0.0 | 15.8 | -7.9 |
| final loss | 0.9626 | 0.9519 | 0.7986 | 0.7873 | |

Wrong-value calls (strict breakdown, `answer_0`): d352 1.7% / 5.0%, d176 32.5% / 13.3% -- the
computation loss is in the CALL. d176 seeds differ by up to 18 pp; two seeds are not a variance
estimate. The pre-registration named `d1` and is NOT tested by this ladder.

## 10. Host/device parity

| | |
|---|---|
| **whole turns, shipped engine (A152/A154)** | **8 of 8 turns identical, 325 ids** -- tool calls executed, results injected, both refusal kinds, an explanation; the same `gencore` loop on both machines; prompt assembly 8/8. Also 8/8 on the group-32 engine. |
| token-for-token, free decoding (A146/A147, defective engine) | 6 prompts, 208 tokens, zero divergences |
| golden probe, shipped engine (`results/golden_hostbuilds_g88p.txt`) | argmax **7 of 8** vs the default host build: step 6 is a near-tie (host top-2 logits 7.4122 / 7.4095, margin 0.0027). Host builds with `-ffp-contract=off` and `=fast` agree with the device **8/8**; the default build disagrees with both of them at the same step |
| sampled logits (l0, l1, lN), shipped engine | host vs device up to **0.061** (default build), 0.081 (`off`), 0.080 (`fast`); host `off` vs host `fast` **0.073** -- two host builds differ by as much as host and device |
| chain hash | differ on every engine: defective 68e759fd... / 2e562d0e...; shipped host `59f6dfc157d37049`, device `f89e1ef535f0ab4e` |
| on the DEFECTIVE engine (for the record) | worst logit deviation 0.048; two host builds 0.098 apart |
| host compiler | results produced with Apple clang 21.0.0 (clang-2100.1.1.101); an Xcode update on 2026-09-26 moved this machine to clang-2100.3.34.2, and whole-turn parity was re-run on the rebuilt decoder: still 8/8 turns, 325 ids; `make check` 95/95 |

**The bit-exactness gate was ill-posed**: there is no unique host answer to match. Token identity
is the claim that holds. Since A152 it holds for WHOLE TURNS: the calculator executes the tool
calls and injects the results itself, through the same `gencore` loop the host scorer runs
(`tools/eval/parity_toolloop.py`, `results/parity_toolloop_g88p.txt`).

## 11. Device inventory

    /chattlm/ChatTLM_Setup.tns       setup, once after every reset (installs the loader, then closes)
    /chattlm/chattlm_support.tns     the loader, read by hardcoded path
    /chattlm/ChatTLM.tns             the app; the student opens it from My Documents
    /chattlm/data/{store,tok4096,model4096}   data -- all three must share one directory

Nothing is in /chattlm/startup (2026-09-27). The loader runs that folder during the install, with
the Setup document still open, and the app started there could not get the model's memory.

## 11b. Appendix B -- the video (media/video/IMG_7083.mov, not in git; see submission/PHOTOS.md)

| | |
|---|---|
| recording | one take, 199.9 s, 4K HEVC 60 fps, no audio, unedited; battery symbol on screen at 15.5 s, no cable in frame |
| question | the Figure 1 question, typed; the APP chose the relation ("Reading Motionally induced emf") |
| answer | `0.01508 V. From epsilon=B*l*v.` -- identical to the host replay (`build/autoasm` then `build/int8gen`) |
| entered | between 121.0 and 121.5 s (frames every 0.5 s) |
| finished | between 180.0 and 180.5 s -- **about 59 s after entry** (58.5 to 59.5), including selection, prompt assembly and tokenization |
| stages | Reading to Thinking 156-157 s; Thinking to Got 166-167 s; Got to Writing 170-171 s (frames every 1 s) |
| paper frames | 15.5 s and 181.5 s, cut by `tools/paper/make_media.py`, hashes recorded there |
| app screens (Figure 7) | drawn by `build/render_screen` (the shipped app.c) from the host decoder's output, recorded in `tools/paper/make_screens.py`; the filmed question's screen matches the 181.5 s frame line for line |

The app's theme follows the clock (dark 18:00-06:00), which is why the video, filmed at 19:19, is dark.

## 12. Open before submission

0. ~~Fix the row-alignment defect?~~ DONE: A153 (commit `edcb002`), hidden padded to 1,056; test_ckpt
   rowalign + rq_probe's row check.
1. ~~Photographs (4 shots planned).~~ DONE 2026-09-27: one video, three frames of it in Appendix B (section 11b).
2. arXiv endorsement -- required since 21 Jan 2026 even with institutional email.
3. Perplexity, host and device. Not measured; the paper must not imply it.
4. Timing variance: decode n = 1 on the shipped engine (three runs, 1.920-1.923, on the defective
   one); most sweep cells n = 1.
5. The selection rules (A156/A157) were designed on the evaluation items; a held-out replication on
   900 fresh items from the same producers is in s8b. What remains unmeasured is questions in
   students' own wording and symbols.
