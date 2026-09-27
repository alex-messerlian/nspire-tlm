# TMLR style files: provenance

Fetched 2026-09-25 from the official repository named in TMLR's author guide
(https://jmlr.org/tmlr/author-guide.html):

- archive: https://github.com/JmlrOrg/tmlr-style-file/archive/refs/heads/main.zip
- repository last pushed 2023-06-30; licence Apache-2.0 (copied as `TMLR_STYLE_LICENSE`)

Copied unmodified:

| file | sha256 |
|---|---|
| `tmlr.sty` | `816214ff5919aa457b6b443bee52b15d9561421417b7f8a50cc84651519f0002` |
| `tmlr.bst` | `306fd454cf40771bee01293eeb98d2c1cd5f4e11ed0cd7296b335f354fc45206` |
| `fancyhdr.sty` | `3d2922548e0e5f1a6c5676eda6ebb6dc20d7d305b4d8c2be5f1c833fb1084e6d` |

Not copied: the example `main.tex`, `main.bib`, `math_commands.tex` (optional notation macros the
paper does not use) and the two example PDFs.

Build modes, one source (`paper.tex`):

- submission: `\usepackage{tmlr}` (anonymous, "Under review as submission to TMLR")
- arXiv: `\usepackage[preprint]{tmlr}`, built by `tools/paper/check_paper.py --preprint` as `paper-arxiv.pdf`
- camera-ready: `\usepackage[accepted]{tmlr}` plus `\month`, `\year`, `\openreview`
