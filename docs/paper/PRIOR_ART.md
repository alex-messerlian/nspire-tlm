# Prior art, and what is actually new here

Searched 2026-09-21. Everything below is a claim about what EXISTS, so each row names its source
and what it does. Where our advantage is narrow, that is said rather than hidden.

## 1. Language models on graphing calculators

| work | what it is | how it differs |
|---|---|---|
| **CalcGPT 3** (FredBennett, Cemetech, **19 Jul 2026**) | 8-bit quantised word-level **GRU**, 640-token vocabulary, 30 hidden states, trained on DailyDialog. Runs in **standard TI-Nspire Python** -- no Ndless. Chat UI, dialogue-act conditioning, ranked generation. Author calls it "a research demonstration, not a reliable assistant: responses often lack semantic understanding." Published sample output: *"I'm just I'm for my yes."*, *"That's true. I am not sure that"*. | The closest existing work and it is **two months old**, so it must be cited and engaged with, not waved past. Differences: GRU vs transformer; ~10^4-10^5 parameters vs 1.09x10^7; interpreted Python vs native ARMv5TE; open-domain chat vs grounded physics; **no throughput, memory or accuracy numbers published**; no tool use, no retrieval. |
| **Transformer On Calc** (stepney141, Kernel/VM Tokyo No. 19, **22 Aug 2026**, slides) | **Same device, same route**: TI-Nspire CX II CAS, native code via **Ndless**, llama2.c-derived TinyStories transformer, **260,032 parameters** (5 layers, dim 64, FFN 172, context 512), FP32 and Q8. Reports ~9 tok/s FP32 and ~16 tok/s Q8 over 64 tokens. No retrieval, tools or conversation. **Power state (battery vs USB) not stated.** | **The most direct precedent of all**: it is the method this project's original brief planned, on this calculator, published a month before this draft. Must be cited prominently in related work. What differs: ~42x the parameters (10.9M vs 0.26M); a domain model trained from scratch with a 4,096 vocabulary rather than the stock stories260K checkpoint; tools, retrieval and context; a measured cost model and memory frontier. **Its timings are NOT a validation of our cost model** -- vocabulary (512 vs 4096), attention layout and power state all differ, and comparing them would be the single-point-fit mistake again. Verified by opening the slides. |
| **CasioLLM** (samilososami, GitHub, **created 15 Aug 2026**, GPL-3.0) | Native `.g3a` add-in on an unmodified **Casio fx-CG50** (SH4). Runs third-party pretrained transformers -- TinyTalk v1 and NanoLM-25M-Instruct (Mistral-compatible, 12 layers, hidden 312) -- vocabulary-pruned to 20,000 and quantised to Q4. **Weights are streamed from 16 MB flash**, not held in RAM; ~512 KB working set. Stateless open-domain chat. Publishes physical timings: `hi` -> 10-token reply, first token 5.695 s, total 27.078 s. README: "not a trustworthy assistant". | **The most comparable prior work, and it invalidates two claims this file used to make** (see s3). It is a transformer, native, offline, on a graphing calculator, with measured timings -- and it needs **no exploit**, because Casio supports add-ins officially, whereas we require an Ndless-derived loader. Differences that survive: third-party chat models vs trained-from-scratch domain model; stateless vs conversational context; no tool use or retrieval; end-to-end latency only vs a per-axis cost model; flash-streamed vs RAM-resident, which is a genuine design alternative our frontier section must discuss -- it escapes our memory ceiling at a large throughput cost. |
| **ChromaLock's ChatGPT TI-84** (Sep 2024, widely covered) | ESP32-C3 Wi-Fi microcontroller **soldered into** a TI-84, talking to the OpenAI API over the 2.5 mm link port. | Not on-device inference at all -- a network terminal. Requires **physical hardware modification** and a live internet connection. We are the opposite on both axes: physically unmodified, fully offline. |

**Honest position, and it should stay in the paper in this form.** We are not the first neural
language model on this calculator; CalcGPT 3 is, by two months. The capability gap is large and
visible in that project's own published screenshots, but the right way to use that is to state
the architectural differences and let the measurements speak -- not to lead with their output
quality. A reviewer who checks the nearest prior work and finds we characterised it fairly will
believe the rest of the paper; one who finds we were dismissive will not.
**AMENDED 2026-09-24: ALL PRIORITY ("FIRST") CLAIMS ARE WITHDRAWN.** This file previously said we
were the first transformer running natively on a graphing calculator and the first to publish
measurements. CasioLLM (above) is a native transformer on a graphing calculator with published
physical timings, and it predates any public version of this work. Both claims were false by the
time they were written; the search that would have found it had simply not been run.

Two lessons, recorded so they are applied at submission time:
1. **This space is moving monthly** -- CalcGPT 3 in July, CasioLLM in August. The prior-art sweep
   is re-run the week of submission, not trusted from September.
2. **The paper states contributions, not priority.** "We present X" survives a prior-art discovery;
   "we are the first to X" does not, and reviewers look for exactly that sentence.

## 2. Tool-augmented small models

The idea that a small model should call a calculator rather than learn arithmetic is established:
**Calc-X**, **ArithmeticGPT**, **MATHSENSEI**, **OccamLLM**, and **T1** (sub-1B models beating
8x larger ones by offloading to a Python interpreter).

**So tool augmentation is not our contribution.** What is new is the regime: every one of those
works runs on a server or a workstation, with a model two to four orders of magnitude larger than
ours, and none of them has a hardware budget that makes the tool call *necessary* rather than
merely *more accurate*. On this device a soft-float multiply runs at 3 MMAC/s against 48 for int8,
so arithmetic in-weights is not just unreliable, it is the expensive option.

## 2b. Academic context on microcontroller LMs (all verified against arXiv, see CITATION_LOG)

Deeploy (arXiv 2408.04413) and MCUBERT (2410.17957) deploy language models within microcontroller
memory budgets; Device Context Protocol (2605.26159) uses an MCU as a tool endpoint for an external
LLM. None runs a tool-calling generative model on the device itself.

**Scope decision (author, 2026-09-24): microcontroller and dev-board projects are out of scope.**
The paper is about calculators. Hobby ESP32 ports (circuitheroesLM, Barista, esp32-llm and
derivatives, llama4micro, AtomeLM, ESP32-S3 Distributed AI, TinyLLM-CIA) are not compared or cited.
Retained: ONE related-work sentence citing the academic MCU deployment papers above, because MLSys
reviewers work in that area and would notice the absence.

## 3. What we claim, precisely -- contributions, not priority

1. **A complete, measured cost model for transformer decoding on ARMv5TE without an FPU** --
   depth (R^2 = 1.000000), width (R^2 = 0.99983) and position (R^2 = 0.99840), validated against
   an independent end-to-end run to 4.1%.
2. **A measured parameters-vs-throughput frontier** with the memory discontinuity located:
   10.9 M parameters at >= 2 tok/s, and the binding constraint is the checkpoint **plus** the KV
   cache, not the checkpoint alone.
3. **An architectural result**: an 11 M-parameter model that is never asked to compute reaches
   100% on grounded computation by emitting tool calls, on hardware where it could not have
   learned the arithmetic.
4. **Negative results reported, not buried**: the model cannot judge whether a retrieved record
   fits the question (1.1%), and explanation quality collapses from 83.5% in-distribution to 8.5%
   on held-out relations.

## 4. Venues

Most of the 2026 cycle has closed (EMNLP demos 10 Jul; NeurIPS workshops 29 Aug; WCCI TinyML
31 Jan).

| venue | deadline | fit |
|---|---|---|
| **arXiv preprint** (cs.LG, cross-list cs.CL) | none -- immediate | Do this first. Establishes the date and gives the repo something to point at. |
| **MLSys 2027** | **30 Oct 2026**, 20:00 UTC | **The target.** Systems venue: measured cost models, quantisation, memory ceilings and deployment on real hardware are exactly its subject. Five and a half weeks out. |
| NeurIPS 2027 "Efficient and On-Device AI Agents" / "On-Device Intelligence" | ~Aug 2027 | Strong thematic fit; a year away. Good home for a fuller version with a trained quality curve. |
| tinyML / EDGE AI Foundation sessions | rolling | Smaller audience, friendly to hardware-first work. |

