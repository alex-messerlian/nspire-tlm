# train/: data preparation, training, and the model files

| file | what it is |
|---|---|
| `prepare.py` | tokenizes the 85/15 mix of synthetic documents and OpenStax text into the flat training stream |
| `select_run.py` | trains one model; the shape, steps and seed come from the environment |
| `lossmask.py` | the loss mask: the tool results the runtime injects are never in the loss |
| `corpus_check.py` | the corpus composition check run before training |
| `test_lossmask.py` | tests for the loss mask |
| `tok4096.json` | the 4,096-entry tokenizer every model below was trained with (`tok_sha c60e1250c64df27e`) |

A checkpoint scored with a different tokenizer scores about zero instead of failing, so every checkpoint carries the
`tok_sha` of its tokenizer and the scoring code refuses a mismatch.

## The model files

The checkpoints are too large for git and are attached to the
[release](https://github.com/alex-messerlian/nspire-tlm/releases). Put them in `train/`. All six are Llama-2-style
decoders with 6 layers, 8 heads, a 512-token context and the tokenizer above, trained with batch 24 and learning rate
3e-4.

| file | what it is | width | steps | seed | corpus |
|---|---|---|---|---|---|
| `ship.pt` | **the model that runs on the calculator**, 10.9M parameters | 352 | 8,000 | 1 | `24c64532f724c6c8` |
| `full_d352.pt` | the same shape trained to 20 tokens per parameter, which did slightly worse (paper, Section 6) | 352 | 17,750 | 1 | `acdc7b70cef192b7` |
| `w352.pt`, `w352s2.pt` | the width ladder at 352 (paper, Appendix A) | 352 | 8,000 | 1, 2 | `acdc7b70cef192b7` |
| `w176.pt`, `w176s2.pt` | the width ladder at 176, 3.09M parameters | 176 | 8,000 | 1, 2 | `acdc7b70cef192b7` |

The corpus column is the `corpus_sha` stamped in each checkpoint, the first 16 hex digits of the SHA-256 of the corpus
it was trained on. `acdc7b70cef192b7` is `corpus/synth_sample.jsonl` as committed (`make corpus` unpacks it);
`24c64532f724c6c8` is the earlier version from the same generator that `ship.pt` was trained on, attached to the
release as `synth_sample_shipped_model.jsonl.gz`.

The calculator's copy of the shipped model is `model4096.bin.tns`, also in the release: `ship.pt` at int8, group 88,
with the hidden width padded from 1,024 to 1,056 so that 88 divides every row. `tools/export_device.sh train/ship.pt 88`
rebuilds it into `build/transfer/`.

## Training

`ship.pt` is the run named `math1`, trained by

```bash
RUN=math1 SEED=1 DIM=352 LAYERS=6 HEADS=8 SEQ=512 BS=24 STEPS=8000 .venv-tok/bin/python train/select_run.py
```

after `train/prepare.py` had built the training stream from its corpus and the OpenStax books (`tools/fetch_oer.sh`).
`select_run.py` has since gained a held-out loss, a heartbeat and periodic checkpoints for the full-length run. The
width-ladder and full-length runs are specified in `docs/PREREG_WIDTH_LADDER.md` and `docs/PREREG_FULL_TRAINING.md`.
