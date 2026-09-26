# train/ -- training code, and which model files matter

Training code lives here (`train.py`, `prepare.py`, ...). So do the model checkpoints, most of which
are too large to keep in git: the ones below are **not tracked**, exist only on disk, and **must be
backed up** (an external drive or cloud storage) until they are released with the paper.

| file | what it is | size |
|---|---|---|
| `ship.pt` | **the model that runs on the calculator** (identical to `archive_a112/math1.pt`, sha256 `0e5ae966...`); d352, 6 layers, 10.9M parameters | 42 MB |
| `archive_a112/tok4096.json` | the tokenizer `ship.pt` was trained with. A checkpoint scored with the wrong tokenizer scores ~0 (`docs/RESULT_TOKENIZER_PAIRING.md`) | small |
| `w176.pt`, `w176s2.pt` | width-ladder models, width 176, seeds 1 and 2 (paper Appendix A) | 12 MB each |
| `w352.pt`, `w352s2.pt` | width-ladder models, width 352, seeds 1 and 2 (paper Appendix A) | 42 MB each |
| `archive_a107/` ... `archive_a120/` | earlier retrains, each with the tokenizer it was trained with | 42-84 MB each |

The calculator's copy of the shipped model is `build/transfer/model4096.bin.tns`: `ship.pt` exported
at int8 group 88 with the hidden width padded to 1,056 (`tools/export_device.sh`). It is rebuilt from
`ship.pt`, so `ship.pt` and its tokenizer are the files that cannot be regenerated.

Older checkpoints (`a46_d352.pt`, `cap_*.pt`, `know*.pt`, ...) are tracked in git from earlier
phases of the project. At publication the weights move to a release (CC BY-NC-SA 4.0, like the
corpus); large files should not keep accumulating in git history.
