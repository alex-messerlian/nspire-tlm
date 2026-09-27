# Training data provenance and licences

Established 2026-09-25 by tracing the shipped pipeline, not from memory. This is the source for
the paper's data section and for the licence on anything released.

**Not legal advice.** It records what the licences say and what the project did; the release
decision in s5 is the author's.

## 1. What the model was trained on

`train/prepare.py` builds the training stream as an **85/15 mix**:

| share | what | where it comes from |
|---|---|---|
| 85% | synthetic documents from `corpus/synth_sample.jsonl` | generated programmatically by `corpus/generate.py` from a record store, question frames, value tables and explanation prose (s3) |
| 15% | raw textbook text | every `.cnxml` module in all 12 OpenStax repositories, markup stripped |

The tokenizer is trained on a sample drawn from both.

**No other dataset enters the shipped pipeline.** Checked by searching `train/prepare.py`,
`train/select_run.py`, `corpus/*.py`, `tools/store_pack.py` and `tools/tok_pack.py`: no FineWeb,
TinyStories, Wikipedia, DailyDialog or `load_dataset` call. (FineWeb appears in the repo's history
only in the tokenizer study, not in what ships.)

## 2. The textbooks, and their licences

`tools/fetch_oer.sh` records the 12 repositories. They contain 24 books. The licence below is read
from **each book's own collection metadata** (`md:license`), which is authoritative per book; the
repository `LICENSE` files agree.

| book | licence |
|---|---|
| Physics | **CC BY 4.0** |
| Precálculo 2ed | **CC BY 4.0** |
| Algebra 1; Algebra and Trigonometry 2e; Astronomy 2e; Calculus Vol. 1-3; Chemistry 2e; Chemistry: Atoms First 2e; College Algebra 2e; College Algebra with Corequisite Support 2e; College Physics 2e; College Physics for AP Courses 2e; Contemporary Mathematics; Elementary Algebra 2e; Intermediate Algebra 2e; Introductory Business Statistics 2e; Introductory Statistics 2e; Prealgebra 2e; Precalculus 2e; University Physics Vol. 1-3 | **CC BY-NC-SA 4.0** |

**22 of 24 books are CC BY-NC-SA 4.0.** This corrects an assumption in the submission checklist,
which said OpenStax is CC BY. Most of it is not.

What CC BY-NC-SA 4.0 asks of us:
- **Attribution** -- credit OpenStax and the books, with the licence and a link. Required.
- **NonCommercial** -- research and free educational use are fine. Selling ChatTLM, or anything
  trained on this data, would not be. Worth knowing before any future plans for the app.
- **ShareAlike** -- adapted material that is *redistributed* must carry the same licence.

## 3. What was derived from the books, and how

| artefact | origin |
|---|---|
| record store (`corpus/store_clean.json` -> `store.tns`, shipped on the calculator) | relations mined from the books, then cleaned by hand-checked passes |
| knowledge tier definitions (`corpus/knowledge/definitions*.json`) | term and meaning text extracted from the books' definition markup |
| explanation prose (`corpus/knowledge/explanations.json`: 531 passages for 177 records; **177 used in training**, one per record, `EXPLAIN_VARIANTS=1`) | **drafted with AI assistance** (see below), grounded in the record's OpenStax section for 127 of 177 records, 50 without one |
| question frames, value tables | hand-written frames; values mined per unit |

**The explanation prose.** The passages were drafted by an AI model from each record's OpenStax
section and then corrected by automated checking passes that were themselves AI-assisted. Before
correction the drafting stage's measured error rate was 13.4%; the rate after correction has not
been measured by a person. In the current file (177 records) 127 are grounded in their OpenStax
section and 50 carry `grounded:false`. **Confirmed by the author (2026-09-26): the author did not
write, edit or read any of these passages, and no person has reviewed them.** The paper states this
in its data section.

## 4. Consequences for the paper

- The data section names OpenStax, lists the books, and states the licences, with the 85/15 split.
- It states how the synthetic corpus was produced, including AI-drafted explanation prose, if
  confirmed.
- `refs.bib` gets an OpenStax entry.

## 5. Release licences -- DECIDED by the author, 2026-09-26

Adopted as below: CC BY-NC-SA 4.0 for corpus, record store, definitions, explanations and model
weights; MIT for the author's own code; MPL 1.1 kept on the Ndless-derived loader; MIT with
Karpathy's notice on llama2.c-derived code.

### The split, as proposed

Anything redistributed that adapts CC BY-NC-SA text should carry CC BY-NC-SA 4.0. The conservative
split:

| artefact | suggested licence | why |
|---|---|---|
| our own code | author's choice (e.g. MIT) | written for this project |
| Ndless-derived loader and `resources/brand.py` | MPL 1.1 | MPL requires modified files stay MPL |
| `runq_nspire.c` and anything from llama2.c | MIT, with Karpathy's notice kept | llama2.c is MIT |
| corpus, record store, definitions, explanations | **CC BY-NC-SA 4.0** | adapted from CC BY-NC-SA books |
| model weights | **CC BY-NC-SA 4.0** (conservative) | whether weights are "adapted material" is legally unsettled; matching the data's licence avoids the question |

The NonCommercial term is the one with real consequences: it permits exactly the educational and
research use this project is for, and rules out selling it.
