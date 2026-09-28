# Licenses

This repository holds work under four licenses. When a file is not named below, it is under the
MIT License in [`LICENSE`](LICENSE).

| what | license |
|---|---|
| the code written for this project, the measurement logs in `results/` and the documentation | MIT, [`LICENSE`](LICENSE) |
| `src/runq_nspire.c`, the inference engine, ported from llama2.c's `runq.c` | MIT, with the llama2.c notice below |
| `installer/`, `resources/brand.py` and `resources/sdk-branding.patch`, derived from Ndless | Mozilla Public License 1.1, [`LICENSES/MPL-1.1.html`](LICENSES/MPL-1.1.html) |
| the record store and everything else in `corpus/` (including `synth_sample.jsonl.gz`), the evaluation items built from them, and the model weights published with the release | [CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/) |
| the paper in `paper/` | CC BY 4.0, [`paper/LICENSE.md`](paper/LICENSE.md), which also covers TMLR's style files |

## Data and weights

The record store, the glossary definitions, the explanation passages and the synthetic training
corpus were built from the OpenStax textbooks listed in
[`docs/paper/DATA_PROVENANCE.md`](docs/paper/DATA_PROVENANCE.md). OpenStax is part of Rice
University. Twenty-two of the books are licensed CC BY-NC-SA 4.0 and two CC BY 4.0, so the adapted
material is released under CC BY-NC-SA 4.0: you may share and adapt it for noncommercial purposes,
with credit, under the same license. The model weights are released under the same license, since
they are trained on that material.

The explanation passages were drafted by an AI model and have not been reviewed by a person; the
paper's data section says how they were made and checked.

## llama2.c

`src/runq_nspire.c` is llama2.c's `runq.c`, ported to the calculator and extended, and
`vendor/llama2.c` (fetched by `tools/fetch_vendor.sh`) supplies the `model.py` the training code
imports. llama2.c's license:

    MIT License

    Copyright (c) 2023 Andrej

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.

## Ndless

ChatTLM runs under Ndless (<https://github.com/ndless-nspire/Ndless>), which the setup document
installs. `installer/` is a fork of Ndless's `ndless/src/installer-6.2`: `gui.lua`,
`Problem1_template.xml`, `Makefile` and `.gitignore` are modified, `stage0.S` differs in the one
line that names the loader's path, and `installer.lua`, `ipc.lua` and `template.sed` are
unchanged (`tools/eval/gate_installer_exploit.py` checks this). `resources/brand.py` renames a copy
of the Ndless loader as ChatTLM's support files, and `resources/sdk-branding.patch` changes three
messages in the Ndless SDK. These files stay under the Mozilla Public License 1.1, the license of
the Ndless sources they modify. The Ndless SDK itself is fetched into `vendor/`, not included here.

The calculator's font is a pre-rendered bitmap of Arial made by `tools/mkfont.py`
(`src/store/font_data.h`); the font program itself is not included.
