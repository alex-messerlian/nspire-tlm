# Device benchmarks

The programs that measured the calculator: every row of [`docs/HARDWARE.md`](../docs/HARDWARE.md),
and the throughput, per-stage and memory figures in the paper. Each one runs on the calculator and
appends its results to `/documents/tlm/bench_results.txt.tns`; the logs pulled back from the device
are in [`results/`](../results/).

| Program | Measures |
|---|---|
| `bench_platform` | the machine itself: clock, caches, timer (HARDWARE.md A1-A12, C1-C8). Run it first. |
| `bench_mem` | the largest allocation and memory bandwidth (A7-A8, B1-B7) |
| `bench_mac` | the multiply-accumulate rate and what a stray `float` costs (B10-B12) |
| `bench_flash` | reading from flash (A9, B8-B9) |
| `bench_rtc` | whether the real-time clock runs, and what its zero means |
| `bench_cas` | whether a program can call the calculator's own computer algebra system |
| `bench_forward` | where one token's time goes, stage by stage |
| `bench_sweep` | per-token time and memory across model shapes, using random weights from `tools/make_shape.py` |
| `bench_ask` | the app's path after enter is pressed, one stage at a time |

## Build and run

From the repository root, with the Ndless toolchain built (see the main README):

```bash
make bench
make bench/bench_sweep.tns
```

`PUSH_BENCH=1 tools/nspire-cli/push-all.sh` sends them to the calculator.

## Two rules these benchmarks enforce for you

**Every record stamps the live CPU clock**, read from the PMU at `0x90140000`. With USB attached the
calculator runs at 288 MHz instead of 396 MHz, and a run taken that way is marked invalid in the log.
Unplug USB before running anything.

**All output goes to a file on the calculator**, not only to the screen, because a benchmark that has
to run with USB disconnected cannot be watched from the host.

## Check the disassembly before believing the numbers

```bash
PATH="$PWD/vendor/Ndless/ndless-sdk/bin:$PWD/vendor/Ndless/ndless-sdk/toolchain/install/bin:$PATH" make -C bench disasm
```

`bench_mem`'s `stream_read_sum` is written as an 8-word unrolled loop so GCC emits `ldmia`, the
widest load an ARM926 has. If it compiled to eight separate `ldr`s instead, B4 is measuring
instruction issue, not memory bandwidth. Likewise `bench_mac`'s `dot_int16_smla` must show
`smlabb`/`smlatt` pairs, and no `__aeabi_` soft-float calls may appear outside `dot_float`.

## Known gaps

- `bench_mem`'s SRAM probe is read-only. Whether a program can safely write to `0xA4000000` (row B7)
  is left untested, since that memory may belong to the OS.
- `bench_flash` B8 reads a file straight after writing it, so the OS page cache may inflate the
  result. It says so in its own output.
- Cache line size is not measured. `bench_platform` reports the CP15-decoded value and a
  latency-against-size sweep; a line-size answer would need a stride sweep at a fixed working set.
