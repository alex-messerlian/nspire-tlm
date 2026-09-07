# nspire-tlm — a language model on a TI-Nspire CX II CAS

Mission, phases, and rules of engagement live in the project log.

## Architecture

**Tool-augmented, not knowledge-augmented.** The model never computes. It emits a structured call,
the runtime executes it, the result is injected, and the model composes prose around it. The model
learns *format*, which is reachable at 45M parameters; it does not learn calculus, which is not.

One base model (language + math register + call format), then continued training per domain —
algebra, calculus, physics, statistics. N finetuned packs ship to flash, one loads into RAM at a
time. Training N models from scratch would pay the English tax N times, and at 45M most of the
capacity is language.

| Doc | What it is |
|---|---|
| [`docs/TOOL_SPEC.md`](docs/TOOL_SPEC.md) | **FROZEN v1.1.0.** The call format. Everything downstream depends on it. |
| [`docs/HARDWARE.md`](docs/HARDWARE.md) | Measured device properties. Populated on hardware. |
| [`docs/RESULT_D416.md`](docs/RESULT_D416.md) | The size question, closed: capability is flat above the threshold. |
| [`docs/RESULT_CANNOT_EXPLAIN.md`](docs/RESULT_CANNOT_EXPLAIN.md) | The corpus, not the device, is the limit. |
| [`docs/RESULT_COMPUTE_BOUND.md`](docs/RESULT_COMPUTE_BOUND.md) | The brief's bandwidth hypothesis, falsified. |
| [`tools/eval/`](tools/eval/) | Backend 1: our own C evaluator, plus the gate suite. |
| [`docs/PRIOR_ART.md`](docs/PRIOR_ART.md) | What already exists. Read this before anything else. |
| [`bench/`](bench/) | The micro-benchmarks that fill in HARDWARE.md. |

## Sequencing

1. ~~Tool interface spec + host evaluator~~ — **done**, no hardware needed.
2. Data generation, **algebra only**.
3. Train **one** model. Prove the full loop on host.
4. Port to device.
5. Only then add domains 2–4.

Do not train four models before one runs on the calculator.

**Current state: Phase 0–2 met on the physical calculator; Phase 3–4 in progress.** The device
generates text unaided, on battery, with no computer attached. Hardware numbers below are measured
unless tagged `[SOURCED]` or `[ESTIMATE]`.

### Measured, on the physical device, battery, USB out

| | |
|---|---|
| Shipping model | **d352 — 10,908,128 parameters**, 6 layers, int8 group-quantised, 11.4 MB |
| Throughput | **1.70 tok/s** at context 512, 1.35 at the larger context (battery, USB out) |
| Largest single `malloc` | 21.56 MiB bare / **4.83 MiB after ordinary use** — the gap is the finding |
| DRAM read | 97 MB/s | 
| Compute | 48 MMAC/s int8 — **the device is compute bound at every quantisation** |
| I-cache / D-cache | 16 KB / **8 KB**, 32-byte line, 4-way |
| CPU clock | **198 MHz, then 144 MHz** — not the 396 MHz the brief assumes |

### The three results the project leads with

1. **A capability threshold, bracketed to 6.1%.** Below ~10.3M parameters the model is healthy in
   every respect — it answers, it refuses a withheld given 100% of the time, it does not
   over-refuse — and it *never* declines on the grounds that the retrieved record does not apply
   (`fit` = 0.0%). Above it, `fit` is 63–68%. The transition is not the memory cliff the brief
   predicted; memory never binds.
2. **Above the threshold, capability is flat, and the corpus is the limit.** +42% parameters buys
   +4.7 points on the primary arm ([`docs/RESULT_D416.md`](docs/RESULT_D416.md)), while a behaviour
   the corpus never demonstrates — explaining what a relation *is* — measured **0.0% at both sizes**
   ([`docs/RESULT_CANNOT_EXPLAIN.md`](docs/RESULT_CANNOT_EXPLAIN.md)). Taught it, the model reaches
   **93.1%** on the records it trains on.
3. **The model reads the record for the tool call and recalls it for prose — and the corpus
   explains exactly why.** Rename one variable in a record's formula *in the prompt only*, on
   records the model knows well, and ask it to restate the relation: it states the **original**
   formula 87–90% of the time and the one it was shown **0 of 80 times.** Give the same corrupted
   record to the *tool call* and it follows what it was shown ~10:1 over memory, on the shipped
   model most strongly of all.
   ([`docs/RESULT_RECALL_NOT_READ.md`](docs/RESULT_RECALL_NOT_READ.md))

   **The corpus forces reading only where the target varies with the input.** A tool call cannot be
   memorised — the values differ in every document — so the model learned to read the formula to
   build one. A prose restatement of that same formula is identical in every document for a given
   record, so recall always suffices and reading is never required. The fix is a corpus property,
   not a capacity one, and the tool call is its own positive control.

## Metrics

Alongside tok/s and perplexity:

- **Tool call validity rate** — emitted calls that parse and execute, split by first attempt vs.
  retry so recovery is visible separately from first-shot accuracy.
- **Answer correctness with tools vs. without** — same checkpoint, same questions, tool layer on and
  off. This difference is the architecture's entire justification, so it is the headline number.
- **Result-span leak rate** — how often the model tries to emit a `<res>` token. Should be ~0. A
  nonzero value means the training loss mask is broken, which is the most dangerous possible bug in
  the pipeline.

---

## Pushback on the brief — and how the predictions scored

The brief asks to be attacked. Four places where I thought it was wrong, **written before any
hardware measurement existed**. They are kept below verbatim, because a prediction is only worth
something if it was recorded before the answer was known. Here is how they did.

| # | Predicted | Measured | Verdict |
|---|---|---|---|
| 1 | int8 roughly balanced, int4 compute bound; *M* ≈ 150 MMAC/s | **97 MB/s DRAM, 48 MMAC/s** — compute bound at **every** quantisation, and memory is cycle-locked to the CPU so the ratio is clock-invariant | **Right, and understated.** The MAC estimate was 3× too high. int4 was dropped for throughput: it halves demand on the resource with headroom |
| 2 | Lead with the frontier, not a parameter record | The project leads with a **capability threshold** and a **flat region above it** | **Held** |
| 3 | Report dense params; lead with the dense number | Shipped dense; `total` and `active` are equal | **Held, and followed** |
| 4 | Capacity binds; KV quantisation and context are first-order | **Compute binds. Memory never does** — the shipping checkpoint uses 48–58% of the ceiling at every context. int8 KV was measured and bought 0.27 tok/s | **Wrong.** Two sessions went into a 21.56 MiB ceiling that never fired |

The §4 estimate — "~70M params at ≈2.1 tok/s, landing on the brief's own 2 tok/s floor" — called its
own coincidence *"either lovely or a sign that one of my estimates is off."* It was the latter, by
about 7×: the real answer is ~10.9M at 1.70 tok/s. The floor itself was later re-examined and
relaxed, because it had been written before anything ran and never compared against a latency anyone
had actually watched.

**The methodology correction was adopted.** Bit-exact greedy tokens at temperature 0 became the port
oracle (`build/golden_forward`), and perplexity is reported with a tolerance rather than claimed
bit-exact — the brief asked for something unachievable across a fp32 host and a fixed-point device.

**The timer note was earned.** The SP804 wrap warning turned out to matter: reading the counter raw,
without the LOAD/CONTROL setup `bench/common.h` does, produced a 2^32 underflow.

The four arguments as originally written follow.

### 1. "Memory bandwidth bound, not compute bound" is probably wrong at int4 on *this* core

The brief's reasoning — int8 gives arithmetic intensity ≈ 1.0, so the machine is bandwidth bound — is
valid arithmetic with a hidden premise: that the core's MAC rate comfortably exceeds its DRAM
bandwidth. That premise holds on almost every modern chip because they all have SIMD. It may not
hold here.

ARM926EJ-S is single-issue with no SIMD. `SMLABB` and friends retire **one 16×16 MAC per cycle**, and
ARMv5TE has no `SMLAD` (dual MAC) — that is ARMv6. So the architectural ceiling is 396 MMAC/s at
396 MHz, and realistic tuned code, once loads and loop overhead are counted, lands well below it.

Let *B* = sustained DRAM read (MB/s), *M* = tuned MAC rate (MMAC/s). For a dense model:

| Quantization | Bytes read per MAC | Memory-bound when |
|---|---|---|
| int8 | 1 | *B* < *M* |
| int4 | 0.5 | 2*B* < *M* |

`[ESTIMATE]` *M* ≈ 150 MMAC/s tuned (≈2.6 cycles/MAC at 396 MHz, allowing for loads and loop
overhead on a core with no out-of-order recovery). *B* is completely unknown — no one has published a
memcpy number for the NS2018 — but an ARM926 with a small D-cache and no prefetcher plausibly
sustains 100–250 MB/s.

Under those estimates **int8 is roughly balanced and int4 is compute bound**, because being
compute-bound at int4 needs only *M* > 2*B*, and *M* > 300 MMAC/s is above the architectural ceiling.

If that holds, the brief's Phase 3 priority order is inverted past the first step. Quantizing int8→int4
is still the right first move — it halves the memory footprint, which is what actually limits model
size — but it will **not** deliver the throughput multiplier the brief predicts, and going below int4
will deliver none at all. Integer kernel tuning, listed third, becomes the highest-value work.

`bench_mac` and `bench_mem` measure *M* and *B* directly. This is HARDWARE.md row **D6**, and it is
the most interesting scientific question in the project — considerably more interesting than "can we
beat 30M parameters."

### 2. The headline result is already gone; the frontier is the real contribution

*(Partly superseded by the tool-augmented architecture: the claim is no longer "largest model" alone
but "largest model that answers correctly," which is a better and less crowded claim. The frontier
argument below still holds.)*

Published **2026-08-03**: a 28.9M-parameter model on an **ESP32-S3** — 240 MHz, 8 MB of slow PSRAM —
at a measured **9.88 tok/s**. See [`docs/PRIOR_ART.md`](docs/PRIOR_ART.md) §2.

The CX II has a faster core and 64 MB of real LPDDR. Exceeding 30M parameters here is close to a
foregone conclusion, so "we beat the Google AI Overview's 30M claim" is no longer a result worth
building a project around — the claim was already false when it was written, and someone else
demonstrated it on weaker hardware.

What is still unclaimed, and what this project should lead with:
- The first measured **params / tok/s / perplexity frontier** for the TI-Nspire CX II.
- The first published **DRAM bandwidth, MAC rate, and usable-heap measurements** for the NS2018 SoC.
  Nobody has these. They are genuinely novel and they are Phase 0 deliverables.
- A **roofline model that predicts the frontier**, and an account of where it fails.

That is a better project than a record attempt, and it is more robust: it cannot be scooped by
someone with a bigger calculator.

### 3. "max(params)" is not well defined once PLE exists

The ESP32-S3 result gets 28.9M total parameters while reading ~450 bytes per token from a
flash-resident embedding table. About 25M of its 28.9M parameters are essentially free, bandwidth-wise.
Per-Layer Embeddings makes parameter count arbitrarily inflatable at fixed cost.

So the mission's objective function needs a decision, made now rather than argued about at the poster:

> **Report both `total_params` and `active_params_per_token`, always, and lead the headline claim with
> the dense number.**

Recommendation: make the primary result a **dense** model, where the two numbers are equal and the
claim is unimpeachable. Then run PLE as a clearly-labelled secondary result — "and with a sparse
embedding architecture, N total parameters at the same throughput." A judge who knows the field will
respect the distinction being drawn explicitly far more than a big number that quietly depends on it.

### 4. The binding constraint is probably capacity, and the "cliff" is a designed experiment

The brief hopes to find the point where the model stops fitting in RAM and throughput falls off a
cliff, calling it the most interesting data point. Two problems.

First, `[ESTIMATE]` with ~35 MB usable heap: int4 weights alone could reach ~70M parameters, but the
KV cache competes for the same RAM. For a llama2-shaped model with dim=512, 8 layers, 1024-token
context, int8 KV is 2·8·1024·512 ≈ **8.4 MB** — a quarter of the budget. Capacity, not bandwidth, is
what stops us, and **KV quantization and context length are therefore first-order levers on maximum
model size**, not the fourth-priority item the brief makes them.

Second, at ~70M params and *M* ≈ 150 MMAC/s, decode is ≈0.47 s/token ≈ **2.1 tok/s** — the capacity
limit and the brief's own 2 tok/s usability floor land in nearly the same place `[ESTIMATE]`. That is
either a lovely coincidence worth putting on the poster, or a sign that one of my estimates is off.
Either way it is worth knowing before committing to a model size.

And the cliff itself: you do not stumble into flash spill: you build it. Deliberately serving weights
from a flash-backed path and measuring the collapse is a **controlled experiment** with a clean
independent variable. Frame it that way rather than as a discovery, and it gets stronger, not weaker.

### One thing the brief asked for that is already solved, and one it missed

**Solved — the timer.** The brief lists "a timer with resolution good enough for per token
measurement" as an open Phase 0 risk. The CX II has an SP804 at `0x90010000` running off the APB
clock, ~99 MHz, giving ~10 ns resolution `[SOURCED]`. That is far better than needed. The real
constraint is that a 32-bit counter at 99 MHz **wraps every ~43 s**, so long runs must handle wrap —
`bench/common.h` does. There are two more SP804s (12 MHz, 32.768 kHz); `bench_platform` gates the
fast one against the 32 kHz one so we never have to trust the wiki's "99 MHz".

**Missed — 256 KB of internal SRAM at `0xA4000000`.** `[SOURCED]` from the Hackspire CX II memory map,
and not mentioned in the brief. If an Ndless application can claim even 64 KB of it, that is the
correct home for activations, RMSNorm scratch, and the hot rows of the KV cache — the data touched
many times per token rather than streamed once. On a machine this bandwidth-constrained that could
matter more than any kernel tuning. Completely unknown whether the OS owns all of it. `bench_mem`
read-probes it; see the safety note in [`docs/PHASE0.md`](docs/PHASE0.md) H7.

### A methodology correction

The brief asks for perplexity "computed identically on host and device to prove the device port is
numerically faithful." Bit-exact perplexity across a host fp32 reference and a device fixed-point
engine is not achievable — the moment softmax, RMSNorm, or RoPE use fixed-point or lookup
approximations, the values diverge by design.

Split the oracle in two:
1. **Bit-exact greedy token sequence at temperature 0.** Achievable, binary pass/fail, and the right
   gate for "is the port correct." Atome lm demonstrates exactly this (48/48 and 16/16 tokens exact)
   on comparable targets — cited precedent for the method.
2. **Perplexity within a stated tolerance**, reported as a number with its tolerance, as the quality
   metric across the quantization sweep.

Conflating them produces a gate that can never be met and would stall Phase 2 indefinitely.

## Getting `corpus/raw` back

`corpus/raw` is 5.1 GB of OpenStax source — twelve books, 2,987 `.cnxml` modules — and it is
**gitignored and not in the repository**. `train/prepare.py` and every miner under `corpus/` read it.

```bash
tools/fetch_oer.sh
```

That script is the inventory: title, upstream repo and on-disk directory for each of the twelve,
including the two where the directory name deliberately differs from the repo name because code
already refers to it. Verify with 2,987 `.cnxml` modules and `prepare.py` reproducing 29,594,922
tokens.

It exists because the directory was once destroyed and there was no script recording what it held —
the inventory survived only as a prose table in `docs/CORPUS_MEASURED.md` and three hardcoded names
in `corpus/tokenizer_study.py`, which turned a two-command restore into archaeology. **A bulk input
with no fetch script is a single point of failure regardless of how it was obtained.**

Do not symlink it from a worktree: a worktree shares its object store with the main checkout, so a
committed symlink is checked out over whatever occupies that path there. That is what destroyed it.
`tools/eval/gate_no_repo_symlink.py` now fails any tracked symlink resolving inside the repo.
