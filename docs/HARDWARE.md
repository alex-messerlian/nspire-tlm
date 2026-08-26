# HARDWARE.md — measured properties of the target device

**Status: MEASURED 2026-08-19. Tethered session + BATTERY CONFIRMATION PASS. Both in
`results/20260819-204006/results.txt` (the log appends).**

**Battery pass result: CPU 396 MHz confirmed (AHB 198, timer 98.98 vs 99.00 expected). Tick counts
match the tethered runs to ≤0.53%, and ≤0.21% at every size ≥2 KB. Derived rates agree to ≤3.6%.
Clock invariance is measured, not inferred. All device numbers below are on solid ground.**
**Raw logs: `results/20260819-202630/`. Four bench_platform runs, one each of mac/flash/mem.**

> ### ⚠ TWO CORRECTIONS THAT AFFECT EVERY NUMBER BELOW
>
> **1. The CPU does NOT run at 396 MHz.** It ran at **198 MHz** on the first run and **144 MHz** on
> every run after. Independently confirmed: the PMU decode and the crystal-gated timer measurement
> agree exactly (timer = CPU/2, twice). The brief's "396 MHz on battery" is not what this device does.
>
> **2. `bench_mac`, `bench_flash` and `bench_mem` hardcoded `timer_hz = 99000000`,** but the timer was
> actually **72 MHz** during those runs (CPU 144 / 2). Every rate they printed is overstated by
> 99/72 = 1.375x and every time understated by the same. **The table below carries CORRECTED values;
> the raw logs carry the uncorrected ones.**

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
| A3 | CPU clock, battery | 396 MHz | [SOURCED] Zephray | **198 MHz once, then 144 MHz — NOT 396** | `bench_platform` |
| A4 | CPU clock, USB attached | 288 MHz | [SOURCED] Zephray | `UNMEASURED` (never benchmarked tethered, by rule) | — |
| A5 | AHB clock | 198 MHz | [SOURCED] Zephray | `UNMEASURED` | — |
| A6 | APB/timer clock | 99 MHz | [SOURCED] Zephray | **exactly CPU/2: 99 MHz @198, 72 MHz @144** | `bench_platform` |
| A7 | RAM | 64 MB LPDDR @ 0x10000000 | [SOURCED] Hackspire CX II | `UNMEASURED` | `bench_mem` |
| A8 | Internal SRAM | 256 KB @ 0xA4000000 | [SOURCED] Hackspire CX II | `UNMEASURED` | `bench_mem` |
| A9 | Flash | 128 MB SPI NAND | [SOURCED] Zephray | `UNMEASURED` | `bench_flash` |
| A10 | I-cache size | 16 KB on older models | [SOURCED] | **16384 B, 32-byte line, 4-way** | `bench_platform` |
| A11 | D-cache size | 8 KB on older models | [SOURCED] | **8192 B, 32-byte line, 4-way** | `bench_platform` |
| A12 | Cache line size | 32 B | [ESTIMATE] | **32 B, confirmed** | `bench_platform` |

> A10–A12 are confirmed by **two independent methods**: the CP15 Cache Type Register
> (`0x1D112152`) and a pointer-chase latency sweep, which steps from 15 ns to 211 ns exactly between
> 8 KB and 16 KB. The CX II has the **same cache as older Nspires**. `cp15_main_id = 0x41069265`
> confirms ARM926EJ-S.
| A13 | CoreMark @ 492 MHz | 1050 (2.1 CM/MHz) | [SOURCED] Zephray | n/a | — |
| A14 | CoreMark @ 396 MHz | 832 | [ESTIMATE] A13 scaled linearly by clock. Assumes CoreMark is compute-bound and cache-resident, which is why it scales; **do not reuse this scaling for memory-bound work.** | `UNMEASURED` | — |

## B. What actually constrains the model — the numbers this project lives or dies on

| # | Property | Why it matters | MEASURED | Bench |
|---|---|---|---|---|
| B1 | Largest single successful `malloc` under Ndless | Hard ceiling on resident weights. A model that does not fit here does not run. | `**22,609,920 B = 21.56 MiB** (pessimistic of 3 battery runs; 22,872,064 / 22,937,600)` | `bench_mem` |
| B2 | Total heap allocatable in chunks | Fragmentation may make B2 >> B1. If so, weights must be allocated per-layer, not as one block. | `**28,442,624 B = 27.12 MiB** (64 KB chunks, 3 battery runs)` | `bench_mem` |
| B3 | Peak RSS of a bare "hello world" Ndless app | Baseline overhead to subtract from every later measurement | `UNMEASURED` | `bench_mem` |
| B4 | Sequential read bandwidth, LPDDR | Memory-bound ceiling | **70.5 MB/s** (corrected; log says 97) | `bench_mem` |
| B5 | `memcpy` bandwidth | Sanity check on B4 | **50.9 MB/s** (corrected; log says 70) | `bench_mem` |
| B6 | Sequential read bandwidth, internal SRAM (0xA4000000) | If we can get even 64 KB of this it is the right home for activations and hot KV rows | `UNMEASURED` MB/s | `bench_mem` |
| B7 | Is internal SRAM usable by an Ndless app at all? | Unknown. OS may own all 256 KB. | `UNMEASURED` (Y/N, bytes) | `bench_mem` |
| B8 | Sequential read, SPI NAND | Cost of any spill | **1.46 MB/s** (corrected; ~48× below B4) | `bench_flash` |
| B9 | Flash latency, random 4 KB | Decides PLE viability | **58 ms** (corrected). **PLE is dead — see D7** | `bench_flash` |
| B10 | int8 MAC rate, naive C | Compute ceiling | **34.9 MMAC/s**, 4.08 cyc/MAC (corrected) | `bench_mac` |
| B11 | int16 MAC via SMLABB | Was expected to beat B10 | **20.4 MMAC/s, 6.92 cyc/MAC — 1.7× SLOWER than naive C** | `bench_mac` |
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

| # | Quantity | Value at the measured 144 MHz |
|---|---|---|
| D1 | Memory-bound tok/s, int8 | `70.5e6 / N` |
| D2 | Memory-bound tok/s, int4 | `141e6 / N` |
| D3 | Compute-bound tok/s | `34.9e6 / N` ← **smallest, therefore binding** |
| D5 | Capacity-bound max params, int4 | ~44M from B1, ~50M from B2 |
| **D6** | **Which binds first** | **COMPUTE. At every quantisation.** |

**D6 — answered, and the brief's hypothesis is falsified.**

Memory-bound requires `B4 < MAC_rate`. Measured: 70.5 MB/s vs 34.9 MMAC/s. **DRAM is twice as fast
as the core can consume**, so the machine is compute-bound at int8 and even more so at int4.

The brief predicted bandwidth. I predicted capacity first, compute second. **Both were wrong** —
compute binds long before either, and it is not close.

| N | tok/s | |
|---|---|---|
| 7M | 4.99 | comfortable |
| 15M | 2.33 | |
| **17.5M** | **2.00** | **the 2 tok/s frontier** |
| 35M | 1.00 | fits in RAM, too slow to demo |
| 45M | 0.78 | fits in RAM, too slow to demo |

**The frontier is ~17.5M parameters, not 30–60M** — and it is set by clock speed, not by memory.
21.9 MB of heap would hold ~44M int4 parameters that would run at 0.8 tok/s.

**Consequence: quantisation below int8 buys nothing for throughput.** Phase 3's priority order is
wrong for this machine. int4 still halves footprint, which matters for capacity, but the throughput
lever is clock and cycles-per-MAC.

| # | Quantity | Value |
|---|---|---|
| D7 | PLE viability | **Dead.** 6 rows/token × 58 ms = 350 ms, i.e. 70% of the entire 500 ms budget at 2 tok/s |
| D8 | Frontier if 396 MHz were reachable | 96 MMAC/s → **2 tok/s at 48M params**. A 2.75× swing. **This is now the highest-value open question in the project.** |

> D6 is the project's thesis. The brief asserts the answer is bandwidth (D2). Our pre-measurement
> reasoning says it is probably capacity (D5) first and compute (D3) second, and that the bandwidth
> claim may be wrong on this specific machine because ARM926EJ-S is a 1-MAC-per-cycle core with no
> SIMD — unusually weak compute relative to its DRAM. See `README.md` §Pushback. **This is a
> disagreement to settle with `bench_mem` and `bench_mac`, not with argument.**

## E. Power and thermal

| # | Property | MEASURED |
|---|---|---|
| E1 | Does the clock drop under sustained load? | **YES, and it never reached 396.** 198 MHz on run 1, then 144 MHz on runs 2, 3 and 4. |
| E2 | Battery level / charge state | **NOT READABLE — measured, see below.** `nsp info`'s "unknown" was ambiguous on its own; a raw probe settles it |
| E3 | Does throughput degrade over a sustained run? | Implied yes by E1; not directly measured |

**E1 is the most consequential finding of the session.** Every compute number above is ~2.75× lower
than the brief assumed, purely because of clock. Whether 396 MHz is reachable — and under what
conditions — now determines the entire frontier.

E1 and E3 exist because a science fair demo runs for a while in a warm gym. If throughput sags, we
need to know before a judge does.

---

## Sources for every `[SOURCED]` row

- Zephray (Wenting Zhang), "On the way to overclock the TI nspire CX II": <https://www.zephray.me/post/on_the_way_to_overclock_nspire_cxii/> — NS2018, ARM926EJ-S, 396/288 MHz CPU, 198 MHz AHB, 99 MHz APB, 64 MB LPDDR, 128 MB SPI NAND, PMU at 0x90140000, 492 MHz overclock, CoreMark 1050.
- Hackspire, "Memory-mapped I/O ports on CX II": <https://www.hackspire.org/Memory-mapped_IO_ports_on_CX_II/> — memory map, SP804 timer bases, SDRAM controller (FTDDR3030) at 0x90120000, PMU at 0x90140000.
- Hackspire, "Hardware": <https://hackspire.org/index.php/Hardware> — 16 KB I-cache / 8 KB D-cache **for pre-CX II models only**.

---

## Battery LEVEL: NOT READABLE. Measured, not assumed.

Distinct from the battery-POWER runs above: this is about reading the charge level, which the web
UI shows for the host machine and the calculator UI shows not at all. That asymmetry rested on a
code comment rather than evidence. Checked from both sides; the answer is no.

**On device — no API exists.** `nm -g` over the BUILT ARCHIVES, not the headers:
`vendor/Ndless/ndless-sdk/lib/libndls.a` (43 global symbols) and `libsyscalls.a` (291) return zero
matches for `batt|charg|power|adc|volt|pmu`. `nm` was sanity-checked against the same archives and
does list `_is_touchpad`, `clrscr`, `cfg_get`, so the tool was working — an empty result from a
broken command is the trap here. `syscall-list.h` enumerates all 343 bridged OS syscalls plus 16
Ndless extensions: zero hits. `driver_aladdin_pmu` and `get_pmu_driver` exist only as IDA
`MakeName` annotations in reverse-engineering inputs — annotated OS-internal addresses, not
exported symbols. `get_battery_door_detection_mode` appears only in classic (non-CX-II) IDC files
and is absent from `OS_cascx2-6.4.0.74.idc`; a door switch is not a charge level in any case.

**Over USB — the OS does not populate the field.** `nsp info` prints "battery unknown", but that
string is the `default:` arm of a three-value switch (`tools/nspire-cli/nsp.c:49`) and would fire
just as readily for an unnamed percentage, so on its own it proves nothing. A raw-byte probe of the
same libnspire call, taken **while connected and charging**:

| field | value |
|---|---|
| `batt_lvl` | `0xFF` (NSPIRE_BATT_UNKNOWN) |
| `is_charging` | `0x00` |

Not a struct-alignment artifact: `batt_lvl` sits at offset 32, between the storage/RAM figures
(0–31) and the version arrays (36–47), and both neighbours decode correctly — storage
62560256/96862208, ram 31629544/35650680, os 6.40.74, boot1 5.0.42, boot2 6.20.7. TI does not fill
the field on CX II.

**The one lead, and why it is probably not one.** PMU register `0x90140810` is readable from user
code and already used two ways (`on_key_pressed()` reads bit 8 as the ON key; `bench/common.h` uses
bit 4 as the clock div2 gate), so MMIO from an Ndless app is proven on this device. But it reads
`0x00000111` in **all 11** recorded device runs across four sessions, spanning both PMU clock
states — not one bit tracked the 396↔288 MHz transition. `0x111` is exactly bit0 + bit4 + bit8 with
every other bit zero, which is itself mild evidence that no charger bit lives in that word. NOT
decisive: USB-attach state was never logged alongside it, and the clock delta may be thermal (E1
reads it that way).

**Consequence for the UI:** no battery indicator on the calculator, and the host server's
`host_stats()` reports THIS MACHINE's battery, which is what its comment already says. Any
calculator battery figure would be invented. Chasing it further is a deliberate device experiment —
log `0x810` with USB in and out, sweep neighbouring PMU words — not a UI task, and it should not
block anything.
