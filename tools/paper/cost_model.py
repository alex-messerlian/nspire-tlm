#!/usr/bin/env python3
"""Every cost-model number in the paper, from ONE measurement block. No number is typed by hand.

    .venv-fig/bin/python tools/paper/cost_model.py [results/device_<session>]

With a session directory (pull-session.sh), reads its bench_results.txt and genlog.txt; without one,
the original results/bench_forward_battery.txt and genlog_battery*.txt.

Two defects in the first draft:

1. MIXED RUNS. The depth fit and the consistency checks came from an earlier block of
   results/bench_forward_battery.txt, the stage shares and the filled-cache slope from a later one.
   This reads only the LAST battery-valid block that contains the filled-cache sweep (part1c), which
   carries its own depth sweep, position sweeps and stage decomposition.

2. WINDOW POSITIONS. bench_forward times REPS=12 consecutive positions and prints the window's FIRST
   position. The depth sweep and the stages run at WARM..WARM+REPS-1 = 3..14. So every point is
   placed at its MEAN position here (start + 5.5, or 8.5), which leaves slopes unchanged and moves
   intercepts to where they belong. Values at position 0 or near the end of the context are
   extrapolations and are labelled so.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

RES = Path(__file__).resolve().parents[2] / "results"
WARM, REPS = 3, 12                       # bench/bench_forward.c
HALF = (REPS - 1) / 2                    # a window starting at s has mean position s + 5.5
STAGE_POS = WARM + HALF                  # depth sweep and stages: positions 3..14, mean 8.5


def fit(xs, ys):
    n = len(xs); mx, my = sum(xs) / n, sum(ys) / n
    b = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / sum((x - mx) ** 2 for x in xs)
    a = my - b * mx
    r2 = 1 - sum((y - (a + b * x)) ** 2 for x, y in zip(xs, ys)) / sum((y - my) ** 2 for y in ys)
    return a, b, r2


SESSION = Path(sys.argv[1]) if len(sys.argv) > 1 else None


def block() -> str:
    t = ((SESSION / "bench_results.txt") if SESSION else RES / "bench_forward_battery.txt").read_text()
    blocks = [b for b in re.split(r"(?=cpu_hz=)", t)
              if "BATTERY(396) -- valid" in b and "t_per_token_pos_filled" in b and "wall_total" in b]
    if not blocks:
        raise SystemExit("no battery-valid block with a filled-cache sweep")
    return blocks[-1]


def decode_runs():
    """(tokens, seconds, forward_only_seconds or None) per decode run. The original files hold
    UNINSTRUMENTED runs; a session's bench_generate always splits decode into rq_forward and argmax
    (A146), which adds timer reads, so for those the forward-only time is reported beside the total
    and the run is marked instrumented."""
    runs = []
    files = ([SESSION / "genlog.txt"] if SESSION else
             [RES / "genlog_battery.txt", RES / "genlog_battery_x3.txt"])
    for f in files:
        blocks = re.split(r"== generate ==", f.read_text())
        if SESSION:
            blocks = blocks[-1:]                      # this session's run only
        for b in blocks:
            if not re.search(r"battery, VALID|BATTERY\(396\) -- valid", b):
                continue
            if not SESSION and "decode split" in b:
                continue
            fw = re.search(r"decode split: in rq_forward \d+ ticks = (\d+) ms", b)
            for n, sec in re.findall(r"decode\s+(\d+) tokens: \d+ ticks = ([\d.]+) s", b):
                runs.append((int(n), float(sec), int(fw.group(1)) / 1000 if fw else None))
    return runs


def main() -> None:
    b = block()
    L = [(int(l), int(us)) for l, us in re.findall(r"t_per_token_L=(\d) layers = \d+ ticks = (\d+) us", b)]
    empty = [(int(p) + HALF, int(us)) for p, us in re.findall(r"t_per_token_pos=pos (\d+) = \d+ ticks = (\d+) us", b)]
    filled = [(int(p) + HALF, int(us)) for p, us in re.findall(r"t_per_token_pos_filled=pos (\d+) = \d+ ticks = (\d+) us", b)]
    stages = {m[0].strip(): (int(m[1]), m[2]) for m in re.findall(r"stage=(\D+?)\s+\d+ ticks\s+(\d+) us\s+\d+%\s+(FIXED|per-layer)", b)}
    wall = int(re.search(r"wall_total=\d+ ticks \((\d+) us\)", b).group(1))

    F0, per_layer, r2L = fit([l for l, _ in L], [us for _, us in L])
    ae, be, r2e = fit(*zip(*empty))
    af, bf, r2f = fit(*zip(*filled))
    fixed = sum(us for us, k in stages.values() if k == "FIXED")
    layer_sum = sum(us for us, k in stages.values() if k == "per-layer")
    attributed = fixed + layer_sum

    print(f"block: {len(L)} depth points, {len(empty)} empty + {len(filled)} filled positions, "
          f"{len(stages)} stages; windows placed at mean positions")
    print(f"depth   (positions 3-14): {F0:,.0f} + {per_layer:,.0f} L us   R^2 = {r2L:.6f}")
    print(f"position, EMPTY cache:  {ae:,.0f} + {be:,.1f} p us   R^2 = {r2e:.6f}")
    print(f"position, FILLED cache: {af:,.0f} + {bf:,.1f} p us   R^2 = {r2f:.6f}   "
          f"(intercept is an extrapolation to p = 0)")
    print(f"filled / empty slope: {bf / be:.3f}  (+{100 * (bf / be - 1):.1f}% relative to empty; "
          f"empty is {100 * (1 - be / bf):.1f}% below filled)")
    print(f"stages (positions 3-14): token {wall:,} us; attributed {attributed:,} us, "
          f"unattributed {100 * (wall - attributed) / wall:.2f}%")
    for name, (us, k) in sorted(stages.items(), key=lambda x: -x[1][0]):
        if us / wall >= 0.01:
            print(f"   {name:12s} {100 * us / wall:5.1f}%")
    mm = sum(stages[s][0] for s in ("ffn matmul", "qkv matmul", "classifier"))
    print(f"   matrix products {100 * mm / wall:.1f}%")
    print("consistency, same block, same positions:")
    print(f"   fixed stages {fixed:,} vs depth intercept {F0:,.0f}: {100 * abs(fixed - F0) / F0:.2f}%")
    print(f"   per-layer stages {layer_sum / 6:,.0f} vs depth slope {per_layer:,.0f}: "
          f"{100 * abs(layer_sum / 6 - per_layer) / per_layer:.2f}%")
    at = af + bf * STAGE_POS
    print(f"   attributed stages {attributed:,} vs filled fit at p = {STAGE_POS} ({at:,.0f}): "
          f"{100 * abs(attributed - at) / at:.2f}%")
    print("throughput, filled model:")
    for p, note in ((0, "extrapolated"), (128, ""), (256, ""), (497.5, "last measured window"),
                    (511, "extrapolated")):
        print(f"   position {p:>5}: {1e6 / (af + bf * p):.3f} tok/s  {note}")
    print(f"   drop 0 -> 511: {100 * (1 - (af + bf * 0) / (af + bf * 511)):.0f}%")
    print("decode prediction (positions 53..53+n-1), filled model, per decode run:")
    runs = decode_runs()
    for n in sorted({n for n, *_ in runs}):
        meas = [s for k, s, _ in runs if k == n]
        fwo = [f for k, _, f in runs if k == n and f is not None]
        pred = sum(af + bf * p for p in range(53, 53 + n)) / 1e6
        pe = sum(ae + be * p for p in range(53, 53 + n)) / 1e6
        m = sum(meas) / len(meas)
        print(f"   {n} tokens: predicted {pred:.3f} s, measured {m:.3f} s (n = {len(meas)}, "
              f"range {min(meas):.3f}-{max(meas):.3f}) -> {100 * (pred - m) / m:+.1f}%; "
              f"empty-cache model {100 * (pe - m) / m:+.1f}%; measured rate {n / m:.3f} tok/s "
              f"(range {n / max(meas):.3f}-{n / min(meas):.3f})"
              + (f"; INSTRUMENTED, forward-only {sum(fwo) / len(fwo):.3f} s -> "
                 f"{100 * (pred - sum(fwo) / len(fwo)) / (sum(fwo) / len(fwo)):+.1f}%" if fwo else ""))


def width_fit() -> None:
    """Per-token cost against parameters, from the LAST valid bench_sweep block of the SAME session
    (one engine; the session log also holds the defective engine's runs under overlapping shape
    names). Positions 11-18, mean 14.5 (bench_sweep: WARM 3, REPS 8). The random-weights d352
    (m352b) is a control on weight dependence, not a width point, so it is left out of the fit.
    N counts the model's parameters, not the zero padding the file adds to the hidden width."""
    if not SESSION:
        return
    blocks = [b for b in (SESSION / "bench_results.txt").read_text().split("=== bench_sweep ===")[1:]
              if "BATTERY(396) -- valid" in b and "HEAP DEPLETED" not in b
              and (m := re.search(r"heap_ceiling_fresh=(\d+) B", b)) and int(m.group(1)) >= (16 << 20)]
    if not blocks:
        print("width: no valid bench_sweep block in this session")
        return
    t = blocks[-1]
    dims = dict(re.findall(r"need=(\S+) dim=(\d+)", t))
    pts = []
    for name, us8 in re.findall(r"shape=(\S+) \d+ B\s+pos8=(\d+) us", t):
        if name == "m352b.bin.tns" or name not in dims:
            continue
        d = int(dims[name]); hidden = -(-(8 * d / 3) // 256) * 256
        n = (4096 * d + 6 * (4 * d * d + 3 * d * hidden)) / 1e6
        pts.append((n, int(us8)))
    pts.sort()
    if len(pts) < 3:
        print(f"width: only {len(pts)} timed shapes -- no fit")
        return
    a, b, r2 = fit([n for n, _ in pts], [us for _, us in pts])
    print(f"width   (positions 11-18, {len(pts)} widths): {a:,.0f} + {b:,.0f} N us, N in millions of "
          f"parameters   R^2 = {r2:.5f}   points " + ", ".join(f"{n:.2f}M {us:,}" for n, us in pts))


if __name__ == "__main__":
    main()
    width_fit()
