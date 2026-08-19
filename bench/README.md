# Phase 0 micro-benchmarks

Four programs. Together they fill in every row of `docs/HARDWARE.md` sections A, B and C.

**None of these have been compiled.** They were written before the toolchain existed. Expect the
first build to fail; that is a normal Phase 0 finding, not a surprise.

| Program | Fills | Answers |
|---|---|---|
| `bench_platform` | A1–A12, C1–C8 | What machine is this? How fast is the clock, really? How big are the caches? Can I trust the timer? |
| `bench_mem` | A7–A8, B1–B7 | How much RAM can I actually have, and how fast can I read it? |
| `bench_mac` | B10–B12 | How many MACs per second, and what does a stray `float` cost? |
| `bench_flash` | A9, B8–B9 | How bad is flash, and is the PLE escape hatch open? |

Run `bench_platform` first. The other three currently hardcode `timer_hz = 99000000` with a TODO;
replace it with the value `bench_platform` measures (row C1) before trusting any MB/s or MMAC/s
figure they print. Each of them logs `timer_hz_assumed` so a stale value is visible in the results
rather than silently wrong.

## Two rules these benchmarks enforce for you

**Every record stamps the live CPU clock**, read from the PMU at `0x90140000`. A run accidentally
taken with USB attached shows ~288 MHz instead of ~396 MHz and is flagged
`USB(288-class) -- RESULT INVALID` in the log. You do not have to remember whether you unplugged.

**All output goes to `/documents/bench/results.txt.tns` on the calculator**, not just the screen,
because a benchmark that must run with USB disconnected cannot be watched from the host. Retrieve the
file afterwards.

## Check the disassembly before believing the numbers

```bash
make disasm
```

`bench_mem`'s `stream_read_sum` is written as an 8-word unrolled loop specifically so GCC emits
`ldmia` — the widest load an ARM926 has. If it compiled to eight separate `ldr`s instead, **B4 is
measuring instruction issue, not memory bandwidth**, and the whole roofline model is built on a wrong
number. Same for `bench_mac`: `dot_int16_smla` must show `smlabb`/`smlatt` pairs, and no `__aeabi_`
soft-float calls may appear outside `dot_float`.

This check takes two minutes and it is the difference between a measurement and a plausible-looking
number.

## Build

Requires the Ndless SDK on `PATH`. See `docs/PHASE0.md` M1.

```bash
make
```

Produces four `.tns` files. Copy them to the calculator, **unplug USB**, run.

## Known gaps

- `bench_mem`'s SRAM probe is **read-only**. Whether an Ndless app can safely *write* to
  `0xA4000000` (row B7) is deliberately left untested — it may belong to the OS. Decide by hand from
  the read probe.
- `bench_flash` B8 reads a file immediately after writing it, so the OS page cache may inflate the
  result. A cold-boot read is the honest measurement; the code says so in its own output rather than
  hiding it.
- Cache *line size* is not measured. `bench_platform` reports the CP15-decoded value and a
  latency-vs-size sweep, but resolving line size needs a separate stride sweep at fixed working-set
  size. Add it if the CP15 decode looks wrong.
