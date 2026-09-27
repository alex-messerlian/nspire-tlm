# The paper's working records

The paper itself is in [`../../paper/`](../../paper/), and what gets uploaded is in
[`../../submission/`](../../submission/). This folder keeps the records behind the paper: where each number comes
from and how each reference was checked. None of it is uploaded.

| File | What it is |
|---|---|
| `FACTS.md` | Every number the paper cites, with the conditions it was measured under and the file it comes from. Check here before changing a number in the paper. |
| `CITATION_LOG.md` | Where each reference was checked at its source. The paper checker requires a row here for every citation. |
| `arxiv_ground_truth.json`, `venue_ground_truth.json` | The arXiv and venue records the citations were checked against. |
| `DATA_PROVENANCE.md` | The training data, where it came from, and its licences. |
| `PRIOR_ART.md` | Earlier language-model projects on calculators, and how the paper describes them. |
| `SUBMISSION_CHECKLIST.md` | The long checklist behind `submission/SUBMISSION.md`, with the source of each rule. |
| `history/` | The first draft, written for MLSys before the move to TMLR, and its plan. Kept as history. |

The scripts that check the paper, draw its figures and compute its cost model are in
[`../../tools/paper/`](../../tools/paper/).
