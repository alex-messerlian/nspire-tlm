# HARDWARE.md — measured properties of the target device

**Status: TEMPLATE. Every value below is `UNMEASURED` until Phase 0 fills it in on the physical device.**

Rule for this file: a number goes in the MEASURED column only when it came off the calculator, on
battery, USB disconnected, with the benchmark source committed. Anything else stays `UNMEASURED` or is
explicitly tagged `[SOURCED]` (someone else measured it, cited) or `[ESTIMATE]` (we derived it, shown work).
Every later document in this repo cites this file rather than restating numbers.

Device under test: TI-Nspire CX II CAS
Serial / revision: `UNMEASURED`
OS version: `UNMEASURED`  ← check first, see Phase 0 step 1
Ndless version installed: `UNMEASURED`
Date of measurement run: `UNMEASURED`

---

## A. Claimed vs. measured

| # | Property | Claimed value | Source of claim | MEASURED | Bench |
|---|---|---|---|---|---|
| A1 | SoC | NS2018 (a.k.a. ET-NS2018-001), undocumented custom | [SOURCED] Zephray | `UNMEASURED` | `bench_platform` |
| A2 | CPU core | ARM926EJ-S, ARMv5TE | [SOURCED] Zephray, Hackspire | `UNMEASURED` | `bench_platform` |
| A3 | CPU clock, battery | 396 MHz | [SOURCED] Zephray | `UNMEASURED` | `bench_platform` |
| A4 | CPU clock, USB attached | 288 MHz | [SOURCED] Zephray | `UNMEASURED` | `bench_platform` |
| A5 | AHB clock | 198 MHz | [SOURCED] Zephray | `UNMEASURED` | `bench_platform` |
| A6 | APB clock | 99 MHz | [SOURCED] Zephray | `UNMEASURED` | `bench_platform` |
| A7 | RAM | 64 MB LPDDR @ 0x10000000 | [SOURCED] Hackspire CX II | `UNMEASURED` | `bench_mem` |
| A8 | Internal SRAM | 256 KB @ 0xA4000000 | [SOURCED] Hackspire CX II | `UNMEASURED` | `bench_mem` |
| A9 | Flash | 128 MB SPI NAND | [SOURCED] Zephray | `UNMEASURED` | `bench_flash` |
| A10 | I-cache size | 16 KB **on classic/CX. CX II unknown — do not assume.** | [SOURCED] Hackspire (older models only) | `UNMEASURED` | `bench_platform` |
| A11 | D-cache size | 8 KB **on classic/CX. CX II unknown — do not assume.** | [SOURCED] Hackspire (older models only) | `UNMEASURED` | `bench_platform` |
| A12 | Cache line size | 32 B typical for ARM926EJ-S | [ESTIMATE] architectural default | `UNMEASURED` | `bench_platform` |
| A13 | CoreMark @ 492 MHz | 1050 (2.1 CM/MHz) | [SOURCED] Zephray | n/a | — |
| A14 | CoreMark @ 396 MHz | 832 | [ESTIMATE] A13 scaled linearly by clock. Assumes CoreMark is compute-bound and cache-resident, which is why it scales; **do not reuse this scaling for memory-bound work.** | `UNMEASURED` | — |

## B. What actually constrains the model — the numbers this project lives or dies on

| # | Property | Why it matters | MEASURED | Bench |
|---|---|---|---|---|
| B1 | Largest single successful `malloc` under Ndless | Hard ceiling on resident weights. A model that does not fit here does not run. | `UNMEASURED` | `bench_mem` |
| B2 | Total heap allocatable in chunks | Fragmentation may make B2 >> B1. If so, weights must be allocated per-layer, not as one block. | `UNMEASURED` | `bench_mem` |
| B3 | Peak RSS of a bare "hello world" Ndless app | Baseline overhead to subtract from every later measurement | `UNMEASURED` | `bench_mem` |
| B4 | Sequential read bandwidth, LPDDR → register, cold | **The single most important number in the project.** Sets the memory-bound tok/s ceiling. | `UNMEASURED` MB/s | `bench_mem` |
| B5 | `memcpy` bandwidth, LPDDR → LPDDR | Sanity check on B4; also the cost of any layout shuffle | `UNMEASURED` MB/s | `bench_mem` |
| B6 | Sequential read bandwidth, internal SRAM (0xA4000000) | If we can get even 64 KB of this it is the right home for activations and hot KV rows | `UNMEASURED` MB/s | `bench_mem` |
| B7 | Is internal SRAM usable by an Ndless app at all? | Unknown. OS may own all 256 KB. | `UNMEASURED` (Y/N, bytes) | `bench_mem` |
| B8 | Sequential read bandwidth, SPI NAND flash | Sets the cost of any weight that spills. Expect 1–2 orders below B4. | `UNMEASURED` MB/s | `bench_flash` |
| B9 | Flash read latency, random 4 KB | Only matters if we go PLE/sparse. See PRIOR_ART §2. | `UNMEASURED` µs | `bench_flash` |
| B10 | int8 MAC rate, naive C matvec | Compute-bound ceiling, before optimization | `UNMEASURED` MMAC/s | `bench_mac` |
| B11 | int16 MAC rate via SMLABB, hand-tuned | Compute-bound ceiling, after optimization. ARMv5TE peak is 1 MAC/cycle. | `UNMEASURED` MMAC/s | `bench_mac` |
| B12 | Soft-float penalty: cycles per `float` multiply | Justifies (or kills) the "zero soft-float in the hot loop" rule | `UNMEASURED` cyc | `bench_mac` |

## C. Timing infrastructure

The CX II exposes three SP804 timers and an RTC. There is **no timing syscall in the Ndless SDK** —
`libndls.h` offers `msleep()` and nothing else. Timing must come from MMIO.

| # | Property | Value | MEASURED |
|---|---|---|---|
| C1 | Fast timer base | 0x90010000, SP804, APB clock (~99 MHz default) [SOURCED] | `UNMEASURED` |
| C2 | Timer 1 base | 0x900C0000, SP804, 12 MHz default [SOURCED] | `UNMEASURED` |
| C3 | Timer 2 base | 0x900D0000, SP804, 32.768 kHz default [SOURCED] | `UNMEASURED` |
| C4 | Chosen benchmark clock source | intend C1 | `UNMEASURED` |
| C5 | Effective tick resolution | ~10.1 ns if C1 runs at 99 MHz [ESTIMATE] | `UNMEASURED` ns |
| C6 | Wrap period of chosen source | ~43.4 s if 32-bit @ 99 MHz [ESTIMATE] — **benchmarks longer than this must handle wrap** | `UNMEASURED` s |
| C7 | Is the chosen timer already owned by the OS? | Unknown. Must check before claiming it. | `UNMEASURED` |
| C8 | Cross-check: C1 vs C3 agree over a 10 s interval | Validates C1's assumed frequency without trusting docs | `UNMEASURED` % error |

## D. Derived ceilings — fill these in automatically once B is measured

None of these are results. They are the roofline model's inputs, computed from section B.

| # | Quantity | Formula | Value |
|---|---|---|---|
| D1 | Memory-bound tok/s, dense int8, N params | `B4 / N` | `UNMEASURED` |
| D2 | Memory-bound tok/s, dense int4, N params | `2 * B4 / N` | `UNMEASURED` |
| D3 | Compute-bound tok/s, N params | `B11 / N` | `UNMEASURED` |
| D4 | Crossover N where D2 == D3 | `2 * B4 / B11` (independent of N) | `UNMEASURED` |
| D5 | Capacity-bound max params, int4 | `2 * (B1 or B2) - KV - activations` | `UNMEASURED` |
| D6 | **Which of D2 / D3 / D5 binds first** | the whole question | `UNMEASURED` |

> D6 is the project's thesis. The brief asserts the answer is bandwidth (D2). Our pre-measurement
> reasoning says it is probably capacity (D5) first and compute (D3) second, and that the bandwidth
> claim may be wrong on this specific machine because ARM926EJ-S is a 1-MAC-per-cycle core with no
> SIMD — unusually weak compute relative to its DRAM. See `README.md` §Pushback. **This is a
> disagreement to settle with `bench_mem` and `bench_mac`, not with argument.**

## E. Power and thermal

| # | Property | MEASURED |
|---|---|---|
| E1 | Does clock drop below 396 MHz during sustained load (thermal/battery)? | `UNMEASURED` |
| E2 | Battery voltage at start vs. end of a 10-minute benchmark | `UNMEASURED` |
| E3 | Does tok/s degrade over a sustained 10-minute run? | `UNMEASURED` |

E1 and E3 exist because a science fair demo runs for a while in a warm gym. If throughput sags, we
need to know before a judge does.

---

## Sources for every `[SOURCED]` row

- Zephray (Wenting Zhang), "On the way to overclock the TI nspire CX II": <https://www.zephray.me/post/on_the_way_to_overclock_nspire_cxii/> — NS2018, ARM926EJ-S, 396/288 MHz CPU, 198 MHz AHB, 99 MHz APB, 64 MB LPDDR, 128 MB SPI NAND, PMU at 0x90140000, 492 MHz overclock, CoreMark 1050.
- Hackspire, "Memory-mapped I/O ports on CX II": <https://www.hackspire.org/Memory-mapped_IO_ports_on_CX_II/> — memory map, SP804 timer bases, SDRAM controller (FTDDR3030) at 0x90120000, PMU at 0x90140000.
- Hackspire, "Hardware": <https://hackspire.org/index.php/Hardware> — 16 KB I-cache / 8 KB D-cache **for pre-CX II models only**.
