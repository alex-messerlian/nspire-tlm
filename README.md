# A Tool-Augmented Language Model on a Graphing Calculator: What It Costs, Where It Fails, and What It Adds

**Alexander Messerlian** · Independent Researcher, Palo Alto, CA, USA ·
ORCID [0009-0003-4933-6832](https://orcid.org/0009-0003-4933-6832)

**Paper:** [`paper/paper.pdf`](paper/paper.pdf), the anonymous copy under review at TMLR, and
[`paper/paper-arxiv.pdf`](paper/paper-arxiv.pdf), the named copy for arXiv.

## Abstract

We built a physics assistant that runs entirely on a TI-Nspire CX II CAS graphing calculator: one ARM926EJ-S core at
396 MHz, no floating-point unit, a largest single memory allocation of about 21.5 MiB and no network. We use it as a
case study in how the conclusions of a small-model evaluation depend on what the evaluation runs. The 10.9M-parameter
model never computes: the runtime picks a relation from a store, puts it in the prompt with the question's values, and
runs the tool call the model writes. Given the right relation, the model is right on 96.7% of problems that give every
value; its errors are digits copied wrongly, and an irrelevant value placed before the relevant ones lowers its score
from 116 to 61 of 120, a pattern consistent with the training generator's habit of adding irrelevant values last. End
to end, on generated problems, choosing the relation was at first the main limit: the system answered 21 of the same
120 problems correctly and declined most of the rest. Two selection rules that use the values the question assigns
raise this to 93 (and to 248 of 300 fresh problems from the same generators), while wrong answers rise from 2 to 4. A
rule-based path through the same runtime with no model is right more often and wrong less often on every set that asks
for a number; the model declines more reliably. Differences between our harness and the calculator moved strict
accuracy by 9 to 21 points and exposed an engine defect that a host–calculator comparison could not detect. A per-
token cost model fitted on battery predicts a decode run within 0.4%.

## On the calculator

<p>
<img src="paper/figures/photo_answer.jpg" height="330" alt="A TI-Nspire CX II CAS on a wooden table, showing the answer 0.01508 V to a question about motional emf">
<img src="paper/figures/screen_emf.png" width="440" alt="The same screen drawn by the application's own code: the question, the relation it chose, and the answer 0.01508 V">
</p>

*Left: a frame from the author's unedited video of the calculator answering the question of the paper's Figure 1, on
battery with no cable attached. Right: the same screen drawn by the application's own code, from the calculator
decoder's output replayed on a computer. The paper's Appendix B has more of both, and the video will be released with
the code.*

## Contents

| Folder | What is in it |
|---|---|
| `paper/` | `paper.pdf` and `paper-arxiv.pdf`, their LaTeX source `paper.tex`, `references.bib`, `figures/`, TMLR's style files, and the paper's license |
| `submission/` | [`SUBMISSION.md`](submission/SUBMISSION.md): what to upload to TMLR and arXiv, what the forms ask for, and what is left to do |
| `src/` | The calculator application and the inference engine, in C |
| `corpus/` | The record store and the program that generates the training documents |
| `train/` | Training code; [`train/README.md`](train/README.md) lists the model files, which are not in git |
| `tools/` | The evaluation harness and its checks (`tools/eval/`), and the paper's checks and figure scripts (`tools/paper/`) |
| `results/`, `device_pull/` | Every measurement behind the paper's tables and figures, and raw logs pulled from the calculator |
| `docs/` | A write-up per result; [`docs/paper/`](docs/paper/) holds the paper's records: every number and its source, and the citation log |
| `bench/` | The calculator's micro-benchmarks |
| `resources/`, `installer/` | The loader derived from Ndless, and the setup document that installs it |

## Editing and rebuilding the paper

Edit `paper/paper.tex`. Keep the `\label{...}` names, the `\ifdeanonymized ... \fi` blocks (they hide the author's name
in the review copy) and `\label{endofmain}` just before the statements (the length check uses it). Every number in the
text is listed in [`docs/paper/FACTS.md`](docs/paper/FACTS.md) with the file it comes from; check it there before
changing it. The tables and figures come from `results/`, so edit their captions freely but their numbers only through
FACTS.md and the scripts.

To rebuild the review copy:

```bash
cd paper && tectonic paper.tex
```

To rebuild both PDFs, write the arXiv upload `submission/arxiv-source.zip`, and run every check (anonymity, citations,
fonts, figures, length), from the repository root:

```bash
.venv-fig/bin/python tools/paper/check_paper.py --final --preprint
```

The environment is `python3 -m venv .venv-fig && .venv-fig/bin/pip install -r tools/paper/requirements.txt`. The
figures are drawn from `results/` by `tools/paper/make_figures.py`.

## Reproducing the results

`make check` builds the host tools and runs the test and check suite. The evaluation scripts are in `tools/eval/`, and
each `docs/RESULT_*.md` describes one result and how it was produced. Timings need the calculator, running on battery,
and the Ndless toolchain; [`build/transfer/README.txt`](build/transfer/README.txt) describes the transfer to the
calculator. The project's working log, from the original brief to the pushback on its hypotheses, is in
[`docs/PROJECT_NOTES.md`](docs/PROJECT_NOTES.md); some of its numbers are superseded, and the paper and FACTS.md are
current.

## Citation

If you use this work, please cite:

```bibtex
@misc{messerlian2026calculator,
  author = {Alexander Messerlian},
  title  = {A Tool-Augmented Language Model on a Graphing Calculator: What It Costs, Where It Fails, and What It Adds},
  year   = {2026},
  note   = {Manuscript under review}
}
```

[`CITATION.cff`](CITATION.cff) gives the same reference.

## License

- **Paper** (everything in `paper/`, including the figures): [CC BY 4.0](paper/LICENSE.md). TMLR's style files keep
  their own licenses.
- **Code, data and weights**: to be released with the final version of the paper, under the MIT License for the code
  written for this work, the Mozilla Public License 1.1 for the loader derived from Ndless, and CC BY-NC-SA 4.0 for the
  corpus, the record store and the weights, following the licenses of the OpenStax source text.

## Contact

Alexander Messerlian, alex.messerlian@icloud.com
