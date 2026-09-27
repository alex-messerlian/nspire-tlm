# Citation log

**Rule: no entry exists in `manuscript/references.bib` (formerly `refs.bib`) without a row here naming the primary source it was taken from.**
arXiv's May 2026 policy bans for hallucinated references, and MLSys requires every author listed.

Every row below was checked independently here against the original record; for arXiv entries
the title and FULL author list in `refs.bib` are copied from the arXiv API response itself
(`arxiv_ground_truth.json`), not transcribed.

ArithmeticGPT is dated 2025, its publication year.

| key | version | primary source checked | authors | date | note |
|---|---|---|---|---|---|
| `openstax` | 12 GitHub repositories, recorded in `tools/fetch_oer.sh` | cloned repositories' own `origin` remotes (`github.com/openstax/osbooks-*`) and each book's `md:title` / `md:license` in `collections/*.collection.xml` | organisation | 2026-09-25 | no individual authors asserted; licences per book from DATA_PROVENANCE.md s2 |
| `trevino2025benchmarking` | NAACL 2025 long | aclanthology.org/2025.naacl-long.149 (title, 5 authors, pages 2916-2934) | 5 | 2026-09-25 | verified at the publisher |
| `shi2023large` | ICML 2023, PMLR 202 | proceedings.mlr.press/v202/shi23a.html (title, 8 authors, pages 31210-31227) | 8 | 2026-09-25 | verified at the publisher |
| `qiu2022mlexray` | MLSys 2022 | proceedings.mlsys.org (title, 8 authors, Proceedings of MLSys 4) | 8 | 2026-09-25 | verified at the publisher |
| `liu2025first` | arXiv:2506.09713v2 | arXiv API (title, 8 authors, v2 updated 2026-01-09; comment says accepted by ACM TOSEM) | 8 | 2026-09-25 | journal version NOT verified, so cited as arXiv |
| `breck2017mltestscore` | IEEE Big Data 2017 | research.google/pubs listing (title, 5 authors, "Proceedings of IEEE Big Data (2017)"); the paper PDF from the same page (title and authors as printed; "Monitor 3: Training and serving features compute the same values" and the training/serving skew discussion quoted) | 5 | 2026-09-26 | pages and DOI NOT verified, so omitted |
| `shi2025retrieval` | Findings of ACL 2025 | aclanthology.org/2025.findings-acl.1258 (title, 7 authors, pages 24497-24524, DOI 10.18653/v1/2025.findings-acl.1258) | 7 | 2026-09-26 | verified at the publisher |
| `jacob2017quantization` | arXiv:1712.05877v1 | arXiv API `export.arxiv.org/api/query?id_list=1712.05877` | 8 | 2026-09-24 | title + full author list taken from the API response itself |
| `lai2018cmsis` | arXiv:1801.06601v1 | arXiv API `export.arxiv.org/api/query?id_list=1801.06601` | 3 | 2026-09-24 | title + full author list taken from the API response itself |
| `zhang2019root` | arXiv:1910.07467v1 | arXiv API `export.arxiv.org/api/query?id_list=1910.07467` | 2 | 2026-09-24 | title + full author list taken from the API response itself |
| `kaplan2020scaling` | arXiv:2001.08361v1 | arXiv API `export.arxiv.org/api/query?id_list=2001.08361` | 10 | 2026-09-24 | title + full author list taken from the API response itself |
| `shazeer2020variants` | arXiv:2002.05202v1 | arXiv API `export.arxiv.org/api/query?id_list=2002.05202` | 1 | 2026-09-24 | title + full author list taken from the API response itself |
| `lewis2020retrieval` | arXiv:2005.11401v4 | arXiv API `export.arxiv.org/api/query?id_list=2005.11401` | 12 | 2026-09-24 | title + full author list taken from the API response itself |
| `lin2020mcunet` | arXiv:2007.10319v2 | arXiv API `export.arxiv.org/api/query?id_list=2007.10319` | 6 | 2026-09-24 | title + full author list taken from the API response itself |
| `su2021roformer` | arXiv:2104.09864v5 | arXiv API `export.arxiv.org/api/query?id_list=2104.09864` | 6 | 2026-09-24 | title + full author list taken from the API response itself |
| `hoffmann2022training` | arXiv:2203.15556v1 | arXiv API `export.arxiv.org/api/query?id_list=2203.15556` | 22 | 2026-09-24 | title + full author list taken from the API response itself |
| `dettmers2022matrix` | arXiv:2208.07339v2 | arXiv API `export.arxiv.org/api/query?id_list=2208.07339` | 4 | 2026-09-24 | title + full author list taken from the API response itself |
| `gao2022program` | arXiv:2211.10435v2 | arXiv API `export.arxiv.org/api/query?id_list=2211.10435` | 8 | 2026-09-24 | title + full author list taken from the API response itself |
| `chen2022program` | arXiv:2211.12588v4 | arXiv API `export.arxiv.org/api/query?id_list=2211.12588` | 4 | 2026-09-24 | title + full author list taken from the API response itself |
| `schick2023toolformer` | arXiv:2302.04761v1 | arXiv API `export.arxiv.org/api/query?id_list=2302.04761` | 8 | 2026-09-24 | title + full author list taken from the API response itself |
| `eldan2023tinystories` | arXiv:2305.07759v2 | arXiv API `export.arxiv.org/api/query?id_list=2305.07759` | 2 | 2026-09-24 | title + full author list taken from the API response itself |
| `touvron2023llama` | arXiv:2307.09288v2 | arXiv API `export.arxiv.org/api/query?id_list=2307.09288` | 68 | 2026-09-24 | title + full author list taken from the API response itself |
| `alizadeh2023flash` | arXiv:2312.11514v3 | arXiv API `export.arxiv.org/api/query?id_list=2312.11514` | 8 | 2026-09-24 | title + full author list taken from the API response itself |
| `liu2024mobilellm` | arXiv:2402.14905v2 | arXiv API `export.arxiv.org/api/query?id_list=2402.14905` | 12 | 2026-09-24 | title + full author list taken from the API response itself |
| `das2024mathsensei` | arXiv:2402.17231v3 | arXiv API `export.arxiv.org/api/query?id_list=2402.17231` | 4 | 2026-09-24 | title + full author list taken from the API response itself |
| `dugan2024occamllm` | arXiv:2406.06576v4 | arXiv API `export.arxiv.org/api/query?id_list=2406.06576` | 6 | 2026-09-24 | title + full author list taken from the API response itself |
| `scherer2024deeploy` | arXiv:2408.04413v1 | arXiv API `export.arxiv.org/api/query?id_list=2408.04413` | 8 | 2026-09-24 | title + full author list taken from the API response itself |
| `yang2024mcubert` | arXiv:2410.17957v1 | arXiv API `export.arxiv.org/api/query?id_list=2410.17957` | 8 | 2026-09-24 | title + full author list taken from the API response itself |
| `kang2025tool` | arXiv:2504.04718v2 | arXiv API `export.arxiv.org/api/query?id_list=2504.04718` | 3 | 2026-09-24 | title + full author list taken from the API response itself |
| `yang2026device` | arXiv:2605.26159v1 | arXiv API `export.arxiv.org/api/query?id_list=2605.26159` | 1 | 2026-09-24 | title + full author list taken from the API response itself |
| `kadlcik2023calcx` | EMNLP 2023 | aclanthology.org/2023.emnlp-main.742.bib | 4 | 2026-09-24 | publisher bib used verbatim; prefer over the arXiv entry `kadlcik2023calcx` duplicates |
| `liu2025arithmeticgpt` | Machine Learning 114:24 | Crossref `api.crossref.org/works/10.1007/s10994-024-06681-1` | 7 | 2026-09-24 | published Jan 2025, not 2024 as first assumed |
| `williams2009roofline` | CACM 52(4) | Crossref `api.crossref.org/works/10.1145/1498765.1498785` | 3 | 2026-09-24 | Crossref title field holds only 'Roofline'; subtitle to be confirmed on the ACM page before submission |
| `stepney141transformeroncalc` | software / project | https://image.docswell.com/s/stepney141/5GN6DJ-kernel-vm-tokyo-19 | - | 2026-09-24 | opened via WebFetch; 260,032 params, 5 layers, dim 64, ctx 512, Ndless, ~9 tok/s FP32 / ~16 tok/s Q8, power state NOT stated |
| `samilososami2026casiollm` | software / project | https://github.com/samilososami/CasioLLM | - | 2026-09-24 | README, MODEL_ATTRIBUTION and results docs read via gh API |
| `fredbennett2026calcgpt` | software / project | https://www.cemetech.net/downloads/files/3024/x4069 | - | 2026-09-24 | archive page opened; 8-bit GRU in TI-Nspire Python |
| `chromalock2024ti32` | software / project | https://github.com/ChromaLock/TI-32 | - | 2026-09-24 | network terminal, hardware modification; cited for contrast only |
| `karpathy2023llama2c` | software / project | https://github.com/karpathy/llama2.c | - | 2026-09-24 | engine derives from runq.c; cite with commit used |
| `ndless` | software / project | https://github.com/ndless-nspire/Ndless | - | 2026-09-24 | loader is a modified Ndless build; citation REQUIRED |

## Published-venue upgrade (2026-09-24)

Each checked here against the PUBLISHER's own record
(proceedings pages' citation metadata, publisher .bib files, or Crossref for DOI registrants).
`venue_ground_truth.json` holds what the publishers returned.

**18 entries now cite the published version.** Where the published version differs from arXiv, the
bibliography uses the published form:

| paper | published differs from arXiv |
|---|---|
| LLM.int8() | **title** is "GPT3.int8(): 8-bit Matrix Multiplication for Transformers at Scale" (NeurIPS 2022) |
| Chinchilla | **title** is "An empirical analysis of compute-optimal large language model training"; **author order** differs (NeurIPS 2022) |
| Toolformer | **adds Eric Hambro** (9 authors vs 8) (NeurIPS 2023) |
| RoFormer | **author order** differs (Neurocomputing 568, 2024) |
| OccamLLM | author name form: "Donato M. Jim\'enez-Benet\'o" (NeurIPS 2024) |

**Stay as arXiv** (no peer-reviewed version found): Llama 2, Scaling Laws, GLU Variants, Device
Context Protocol. **Unresolved, cited as arXiv**: TinyStories (OpenReview returned a bot challenge,
not bypassed), CMSIS-NN (the only Crossref match was a different 2026 paper -- rejected, not used).
Duplicate arXiv Calc-X entry removed; the EMNLP 2023 entry remains.


| key | version | primary source checked | authors | date | note |
|---|---|---|---|---|---|
| `dugan2024occamllm` | 2406.06576 -> Advances in Neural Information Processing Systems 2024 | https://proceedings.neurips.cc/paper_files/paper/2024/hash/3eceb70f47690051d6769739fbf6294b-Abstract-Conference.html | publisher | 2026-09-24 | published version; title/authors from publisher record |
| `kang2025tool` | 2504.04718 -> International Conference on Learning Representations 2026 | https://proceedings.iclr.cc/paper_files/paper/2026/hash/776a5f2c7d6dd4b0d83145fc044e2726-Abstract-Conference.html | publisher | 2026-09-24 | published version; title/authors from publisher record |
| `lin2020mcunet` | 2007.10319 -> Advances in Neural Information Processing Systems 2020 | https://proceedings.neurips.cc/paper/2020/hash/86c51678350f656dcc7f490a43946ee5-Abstract.html | publisher | 2026-09-24 | published version; title/authors from publisher record; the source prints one author as "john cohn", capitalized in `references.bib` as John Cohn (2026-09-27) |
| `chen2022program` | 2211.12588 -> Transactions on Machine Learning Research 2023 | https://jmlr.org/tmlr/papers/bib/YfZ4ZPt8zd.bib | publisher | 2026-09-24 | published version; title/authors from publisher record |
| `dettmers2022matrix` | 2208.07339 -> Advances in Neural Information Processing Systems 2022 | https://proceedings.neurips.cc/paper_files/paper/2022/hash/c3ba4962c05c49636d4c6206a97e9c8a-Abstract-Conference.html | publisher | 2026-09-24 | published version; title/authors from publisher record |
| `zhang2019root` | 1910.07467 -> Advances in Neural Information Processing Systems 2019 | https://proceedings.neurips.cc/paper/2019/hash/1e8a19426224ca89e83cef47f1e7f53b-Abstract.html | publisher | 2026-09-24 | published version; title/authors from publisher record |
| `hoffmann2022training` | 2203.15556 -> Advances in Neural Information Processing Systems 2022 | https://proceedings.neurips.cc/paper_files/paper/2022/hash/c1e2faff6f588870935f114ebe04a3e5-Abstract-Conference.html | publisher | 2026-09-24 | published version; title/authors from publisher record |
| `schick2023toolformer` | 2302.04761 -> Advances in Neural Information Processing Systems 2023 | https://proceedings.neurips.cc/paper/2023/hash/d842425e4bf79ba039352da0f658a906-Abstract-Conference.html | publisher | 2026-09-24 | published version; title/authors from publisher record |
| `jacob2017quantization` | 1712.05877 -> Proceedings of the IEEE Conference on Computer Vision and Pattern Recognition 2018 | https://openaccess.thecvf.com/content_cvpr_2018/html/Jacob_Quantization_and_Training_CVPR_2018_paper.html | publisher | 2026-09-24 | published version; title/authors from publisher record |
| `lewis2020retrieval` | 2005.11401 -> Advances in Neural Information Processing Systems 2020 | https://proceedings.neurips.cc/paper/2020/hash/6b493230205f780e1bc26945df7481e5-Abstract.html | publisher | 2026-09-24 | published version; title/authors from publisher record |
| `liu2024mobilellm` | 2402.14905 -> International Conference on Machine Learning 2024 | https://proceedings.mlr.press/v235/liu24ce.html | publisher | 2026-09-24 | published version; title/authors from publisher record |
| `gao2022program` | 2211.10435 -> International Conference on Machine Learning 2023 | https://proceedings.mlr.press/v202/gao23f.html | publisher | 2026-09-24 | published version; title/authors from publisher record |
| `alizadeh2023flash` | 2312.11514 -> Proceedings of the 62nd Annual Meeting of the Association for Computational Linguistics (Volume 1: Long Papers) 2024 | https://aclanthology.org/2024.acl-long.678.bib | publisher | 2026-09-24 | published version; title/authors from publisher record |
| `das2024mathsensei` | 2402.17231 -> Proceedings of the 2024 Conference of the North American Chapter of the Association for Computational Linguistics: Human Language Technologies (Volume 1: Long Papers) 2024 | https://aclanthology.org/2024.naacl-long.54.bib | publisher | 2026-09-24 | published version; title/authors from publisher record |
| `su2021roformer` | 2104.09864 -> via Crossref  | DOI in entry | publisher | 2026-09-24 | published version; title/authors from publisher record |
| `yang2024mcubert` | 2410.17957 -> via Crossref  | DOI in entry | publisher | 2026-09-24 | published version; title/authors from publisher record |
| `scherer2024deeploy` | 2408.04413 -> via Crossref  | DOI in entry | publisher | 2026-09-24 | published version; title/authors from publisher record |

## Still to do before submission

- [x] **Published-venue versions** (done for 18; see above).
- [ ] ~~Published-venue versions.~~ Most arXiv entries also appeared at a conference or journal
  (e.g. NeurIPS, ICML, TMLR). Reviewers expect the published version cited. Each venue must be
  confirmed on the publisher's own page before the entry is upgraded -- not taken from memory.
- [x] **Duplicates.** arXiv Calc-X removed.
- [x] **Roofline subtitle** confirmed via Crossref: "an insightful visual performance model for multicore architectures".
- [ ] **Software entries** get the exact commit or release used, not just the repository.
- [ ] **Prune.** The .bib is the verified superset. Only what the paper actually cites ships.
- [ ] Other prior-art projects (Barista, esp32-llm and derivatives, llama4micro, AtomeLM, ...) are
  **unverified here and uncited**. Any that the text will cite gets a row first.

**2026-09-25, URL normalisation.** Three `url` fields pointed at BibTeX export files rather than the
paper page. Replaced and each checked at the source: `das2024mathsensei` ->
aclanthology.org/2024.naacl-long.54 (title and 4 authors match); `alizadeh2023flash` ->
aclanthology.org/2024.acl-long.678 (title and 8 authors match); `chen2022program` ->
openreview.net/forum?id=YfZ4ZPt8zd, the `url` in TMLR's own BibTeX for the paper.
