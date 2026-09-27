#!/usr/bin/env python3
"""Generate every figure in the paper FROM THE RAW RESULT FILES.

No number here is typed in. Each figure reads a log that a device instrument wrote (or that the
scorer printed), so re-running this after a new device session updates every figure, and a figure
cannot drift from the data it claims to show.

    .venv-fig/bin/python tools/paper/make_figures.py

Style follows the dataviz method: at most three categorical slots (the only count that validates on
every pair), validated against the white page with the skill's validator -- all hard gates PASS; the
third slot (aqua) is under 3:1 contrast on white, so wherever it appears it carries a visible direct
label. Distinct marker shapes are a second identity channel, so the figures survive greyscale print.
Hairline recessive gridlines, text in ink tokens never in series colour, a legend only when there
are two or more series.
"""
from __future__ import annotations

import math
import re
from pathlib import Path

import matplotlib

matplotlib.use("pdf")
import matplotlib.pyplot as plt  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
RES = ROOT / "results"
# Written INTO the paper folder, which must build on its own (paper/). The PNG previews are not
# part of the paper and go to build/, so paper/ holds only what the manuscript uses.
OUT = ROOT / "paper" / "figures"
PREVIEW = ROOT / "build" / "paper-figures"
PREVIEW_ON = True   # off when drawing into another folder (check_paper.py compares against it)
# THE SHIPPED ENGINE'S DEVICE SESSION (padded group 88, A153/A154). The device log is append-only and
# this file holds every run the calculator ever wrote, the defective unpadded engine's included; only
# the LAST bench_forward and the LAST valid bench_sweep block describe the shipped engine.
SESSION = RES / "device_g88p" / "bench_results.txt"
# The group-32 session, read ONLY for the width-384 load (A151): the one configuration that held
# more than the largest single block (22.10 MiB in two allocations) and loaded. Plotted as a
# different layout, never mixed into the throughput axes.
G32_SESSION = RES / "device_g32" / "bench_results.txt"

# ---- tokens (dataviz reference palette; surface is the white page) ---------------------------
SURFACE = "#ffffff"
INK, INK2, MUTED = "#0b0b0b", "#52514e", "#898781"
GRID, AXIS = "#e1e0d9", "#c3c2b7"
S1, S2, S3 = "#2a78d6", "#eb6834", "#1baf7a"          # blue, orange, aqua -- fixed order
MARKERS = ("o", "s", "^")

COL_W = 3.15         # half the TMLR text width: figures are set in pairs at 0.49\linewidth
PT = 1 / 72


def style() -> None:
    plt.rcParams.update({
        "font.size": 7.5, "axes.titlesize": 8, "axes.labelsize": 7.5,
        "xtick.labelsize": 7, "ytick.labelsize": 7, "legend.fontsize": 7,
        "axes.edgecolor": AXIS, "axes.linewidth": 0.6, "axes.labelcolor": INK2,
        "xtick.color": MUTED, "ytick.color": MUTED, "text.color": INK,
        "axes.grid": True, "grid.color": GRID, "grid.linewidth": 0.5, "grid.linestyle": "-",
        "axes.axisbelow": True, "axes.spines.top": False, "axes.spines.right": False,
        "figure.facecolor": SURFACE, "axes.facecolor": SURFACE, "savefig.facecolor": SURFACE,
        "legend.frameon": False, "pdf.fonttype": 42,   # embed TrueType, as venues require
    })


LADDER_RUNS = ("w176", "w176s2", "w352", "w352s2")


def inputs() -> dict:
    """figure -> the files it is drawn from, as a record for readers. The freshness check no longer
    uses it: tools/paper/check_paper.py redraws every figure and compares bytes."""
    ladder = [RES / f"{k}_int8_g16_{n}.json" for n in LADDER_RUNS for k in ("correct", "arms")]
    return {"fig_position": [SESSION], "fig_stages": [SESSION], "fig_frontier": [SESSION],
            "fig_memory": [SESSION, G32_SESSION], "fig_ladder": ladder}


# ---- parsers -----------------------------------------------------------------------------------
def last_block(text: str, header: str) -> str:
    """The LAST run of an instrument in an append-only log. Earlier runs in sweep_battery.txt were
    taken on a depleted heap or with shapes the engine now refuses, and must not be plotted."""
    parts = text.split(header)
    if len(parts) < 2:
        raise SystemExit(f"no '{header}' block found")
    return parts[-1]


def parse_forward() -> dict:
    t = last_block(SESSION.read_text(), "=== bench_forward ===")
    assert "BATTERY(396) -- valid" in t, "bench_forward block is not a valid battery run"
    # EACH POINT IS A 12-POSITION WINDOW AVERAGE printed at its START (bench_forward.c, REPS=12), so it
    # is placed at the window's MEAN, start + 5.5. The first draft plotted it
    # at the start, which left every slope right and every intercept 5.5 positions off.
    pos = [(int(p) + 5.5, int(us)) for p, us in re.findall(r"t_per_token_pos=pos (\d+) = \d+ ticks = (\d+) us", t)]
    # A147: the same windows with the KV cache FILLED by a sequential walk -- the realistic cost.
    filled = [(int(p) + 5.5, int(us)) for p, us in re.findall(r"t_per_token_pos_filled=pos (\d+) = \d+ ticks = (\d+) us", t)]
    stages = [(n.strip(), int(us), lbl) for n, us, lbl in
              re.findall(r"stage=(.+?)\s+\d+ ticks\s+(\d+) us\s+\d+%\s+(FIXED|per-layer)", t)]
    wall = int(re.search(r"wall_total=\d+ ticks \((\d+) us\)", t).group(1))
    return {"pos": pos, "filled": filled, "stages": stages, "wall": wall}


def parse_sweep() -> dict:
    """The LAST valid bench_sweep block of the shipped-engine session, and nothing else.

    This used to merge the newest valid measurement per shape across every block in the log. That
    was right while one engine had written the log; the session log now holds the defective and the
    padded engine's runs under overlapping shape names (m176 is 3,236,032 B unpadded and 3,289,024 B
    padded), and a merge would put two engines on one axis. One block is one engine.
    """
    blocks = SESSION.read_text().split("=== bench_sweep ===")[1:]
    valid = [t for t in blocks if "BATTERY(396) -- valid" in t and "HEAP DEPLETED" not in t
             and (m := re.search(r"heap_ceiling_fresh=(\d+) B", t)) and int(m.group(1)) >= (16 << 20)]
    if not valid:
        raise SystemExit("no valid bench_sweep block (battery, non-depleted) in the session log")
    t = valid[-1]
    need = {m[0]: {"dim": int(m[1]), "hidden": int(m[2]), "file": int(m[3]), "kv": int(m[4]),
                   "fits": m[5] == "FITS"}
            for m in re.findall(r"need=(\S+) dim=(\d+) hidden=(\d+) .*? file=(\d+) B kv=(\d+) B total=\d+ B (FITS|DOES NOT FIT)", t)}
    timed = {m[0]: {"us8": int(m[1]), "us256": int(m[2]), "per_pos": int(m[3])}
             for m in re.findall(r"shape=(\S+) \d+ B\s+pos8=(\d+) us\s+pos256=(\d+) us\s+per_pos=(-?\d+) us", t)}
    padded = [n for n, v in need.items() if v["hidden"] % 88]
    if padded:
        raise SystemExit(f"ABORT: {padded} have a hidden width 88 does not divide -- not the shipped layout")
    ceiling = int(re.search(r"heap_ceiling_fresh=(\d+) B", t).group(1))
    return {"ceiling": ceiling, "need": need, "timed": timed, "blocks": 1}


def params_for(dim: int) -> int:
    hidden = math.ceil((8 * dim / 3) / 256) * 256
    return 4096 * dim + 6 * (4 * dim * dim + 3 * dim * hidden)


def parse_ladder() -> dict:
    """{width: {arm: [pct per training seed, ...]}} from the DEVICE-DECODER results at group 16.

    Scored by tools/eval/score_on_device_decoder.sh: int8, greedy, the app's tool loop, prompts from
    the device's own assembler. Group 16 divides every row length at both widths, so the engine's
    row-alignment defect at group 88 (docs/RESULT_CORRECTNESS.md) does not confound width.
    The earlier arms_width_ladder*.txt files were fp32, sampled at T=0.8, on the split's stale prompt
    text -- and reported the refusal arm `d1` as "grounded compute". They are not read."""
    import json
    out: dict[int, dict[str, list[float]]] = {}
    for name in LADDER_RUNS:
        c = json.loads((RES / f"correct_int8_g16_{name}.json").read_text())
        a = json.loads((RES / f"arms_int8_g16_{name}.json").read_text())
        dim = int(c["dim"])
        d = out.setdefault(dim, {})
        d.setdefault("compute", []).append(c["arms"]["answer_0"]["device_ok_pct"])
        for arm in ("d1", "explain", "fit"):
            d.setdefault(arm, []).append(a["arms"][arm]["pct"])
    return out


# ---- figures -----------------------------------------------------------------------------------
def _ols(pts):
    xs = [p for p, _ in pts]; ys = [us / 1000 for _, us in pts]
    n = len(xs); mx, my = sum(xs) / n, sum(ys) / n
    b = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / sum((x - mx) ** 2 for x in xs); a = my - b * mx
    r2 = 1 - sum((y - (a + b * x)) ** 2 for x, y in zip(xs, ys)) / sum((y - my) ** 2 for y in ys)
    return xs, ys, a, b, r2


def fig_position(fw: dict) -> str:
    """Per-token cost vs position, with the KV cache empty (the old sweep) and FILLED (real decode).
    The gap between the two lines is the finding: attention over zero rows is cheaper on soft-float."""
    fig, ax = plt.subplots(figsize=(COL_W, 2.0))
    series = [("filled cache (real decode)", fw["filled"], S1, "o"),
              ("empty cache", fw["pos"], S2, "s")]
    for label, pts, col, mk in series:
        if not pts:
            continue
        xs, ys, a, b, r2 = _ols(pts)
        ax.plot([xs[0], xs[-1]], [a + b * xs[0], a + b * xs[-1]], color=col, lw=1.5, zorder=2)
        ax.scatter(xs, ys, s=22, color=col, marker=mk, edgecolor=SURFACE, linewidth=1.2, zorder=3,
                   label=f"{label}, {b * 1000:,.0f} µs/pos")
    ax.set_xlabel("position in context"); ax.set_ylabel("ms per token")
    ax.set_xlim(0, 520); ax.set_ylim(0, None)
    ax.legend(loc="lower right")
    return save(fig, "fig_position")


# The device log's stage names, as the paper's text names them.
STAGE_NAME = {"ffn matmul": "feed-forward", "qkv matmul": "q, k, v projections",
              "RoPE": "rotary embeddings"}


def fig_stages(fw: dict) -> str:
    total = fw["wall"]            # measured token time; shares are computed, never read from the log
    major = sorted([(n, us) for n, us, _ in fw["stages"] if us / total >= 0.02], key=lambda s: s[1])
    other = total - sum(us for _, us in major)      # 7 small stages + 0.3% unattributed
    rows = [("everything else", other)] + major
    fig, ax = plt.subplots(figsize=(COL_W, 1.7))
    y = range(len(rows))
    ax.barh(list(y), [us / total * 100 for _, us in rows], height=0.55, color=S1, edgecolor=SURFACE, linewidth=0)
    ax.set_yticks(list(y), [STAGE_NAME.get(n, n) for n, _ in rows], color=INK2)
    for i, (_, us) in enumerate(rows):
        ax.text(us / total * 100 + 1, i, f"{us / total * 100:.1f}%", va="center", color=INK, fontsize=7)
    ax.set_xlabel("% of per-token time"); ax.set_xlim(0, 68)
    ax.grid(axis="y", visible=False)
    return save(fig, "fig_stages")


def fig_frontier(sw: dict) -> str:
    # A143: the 8-head d264 checkpoint had an odd head size; its timing is NOT reported.
    pts = []
    for name, t in sw["timed"].items():
        if name == "m352b.bin.tns":   # the random-weights d352: a control, not a frontier point
            continue
        dim = sw["need"][name]["dim"]
        # bench_sweep times positions 11-18 and 259-266 (WARM 3, REPS 8) under the labels 8 and 256:
        # the intercept is taken from the first window's MEAN position, 14.5.
        a = t["us8"] - 14.5 * t["per_pos"]
        pts.append((params_for(dim) / 1e6, dim, a, t["per_pos"]))
    pts.sort()
    fig, ax = plt.subplots(figsize=(COL_W, 2.1))
    # Positions 8 and 256 are the two MEASURED points per shape (Table 2 quotes the same two);
    # 128 is linear between them and is labelled as interpolated rather than passed off as measured.
    for (pos, col, mk, lab) in zip((14.5, 128, 262.5), (S1, S2, S3), MARKERS,
                                   ("positions 11\u201318", "position 128 (interpolated)", "positions 259\u2013266")):
        xs = [p for p, *_ in pts]; ys = [1e6 / (a + pos * s) for _, _, a, s in pts]
        ax.plot(xs, ys, color=col, lw=1.5, marker=mk, ms=4.5, mec=SURFACE, mew=1.0, label=lab,
                linestyle="--" if pos == 128 else "-")
    ax.axhline(2.0, color=MUTED, lw=0.8, zorder=1)
    ax.text(0.3, 2.08, "2 tok/s", color=MUTED, fontsize=6.5, va="bottom")
    nf = sorted({(params_for(v["dim"]) / 1e6, v["dim"]) for v in sw["need"].values() if not v["fits"]})
    for x, d in nf:     # a TESTED failure: the engine tried to load it on hardware and could not
        ax.scatter([x], [0.35], marker="x", s=28, color=INK2, linewidth=1.2, zorder=3)
        ax.text(x, 0.75, f"width {d}\ndid not load", ha="center", va="bottom", color=INK2, fontsize=6.5)
    nf = [x for x, _ in nf]
    ax.set_xlabel("parameters (millions)"); ax.set_ylabel("tokens per second")
    ax.set_xlim(0, max(nf + [pts[-1][0]]) + 1.5); ax.set_ylim(0, None)
    ax.legend(loc="upper right")
    return save(fig, "fig_frontier")


def g32_d384() -> dict:
    """The width-384 group-32 shape from the last bench_sweep block that timed it: sizes, and the
    fact that it LOADED (a timing line exists only for a shape the engine loaded)."""
    for b in reversed(G32_SESSION.read_text().split("=== bench_sweep ===")[1:]):
        m = re.search(r"need=m384\.bin\.tns dim=(\d+) .*? file=(\d+) B kv=(\d+) B", b)
        if m and "BATTERY(396) -- valid" in b:
            return {"dim": int(m.group(1)), "file": int(m.group(2)), "kv": int(m.group(3)),
                    "fits": re.search(r"shape=m384\.bin\.tns \d+ B\s+pos8=", b) is not None,
                    "layout": "group 32"}
    raise SystemExit("no battery-valid width-384 record in the group-32 session")


def fig_memory(sw: dict) -> str:
    """Checkpoint + fp32 KV cache per width, and WHERE THE TWO-BLOCK LIMIT LIES.

    The first version drew the largest single block (21.53 MiB) across a stacked
    SUM of two allocations, which reads as a limit on the sum -- and width 384 held 22.10 MiB in two
    blocks and loaded. The only honest statement about the sum is a RANGE: above the largest total
    that loaded and below the smallest that did not. That range is what is shaded now."""
    rows = sorted(sw["need"].values(), key=lambda v: v["dim"])
    seen, uniq = set(), []
    for r in rows:
        if r["dim"] not in seen:
            seen.add(r["dim"]); uniq.append(dict(r, layout="padded group 88"))
    uniq.append(g32_d384())
    uniq.sort(key=lambda r: r["dim"])
    MiB = 2 ** 20
    fig, ax = plt.subplots(figsize=(COL_W, 1.95))
    x = range(len(uniq))
    ck = [r["file"] / MiB for r in uniq]; kv = [r["kv"] / MiB for r in uniq]
    hatch = ["//" if r["layout"] == "group 32" else "" for r in uniq]
    ax.bar(list(x), ck, width=0.5, color=S1, edgecolor=SURFACE, linewidth=1.2, label="checkpoint",
           hatch=hatch)
    ax.bar(list(x), kv, bottom=ck, width=0.5, color=S2, edgecolor=SURFACE, linewidth=1.2,
           label="fp32 KV cache", hatch=hatch)
    tot = [(r["file"] + r["kv"]) / MiB for r in uniq]
    lo = max(t_ for t_, r in zip(tot, uniq) if r["fits"])
    hi = min(t_ for t_, r in zip(tot, uniq) if not r["fits"])
    ax.axhspan(lo, hi, color=GRID, alpha=0.9, zorder=0, linewidth=0)
    ax.text(-0.35, hi + 0.5, f"limit on the total: {lo:.1f} to {hi:.1f} MiB", ha="left",
            color=INK2, fontsize=6.5)
    for i, r in enumerate(uniq):
        ax.text(i, tot[i] + 0.4, f"{tot[i]:.1f}", ha="center", color=INK, fontsize=6.5)
    ax.set_xticks(list(x), [f"{r['dim']}\n{'loads' if r['fits'] else 'did not load'}" for r in uniq])
    ax.set_xlabel("width (384: group-32 layout, hatched)")
    ax.set_ylabel("MiB held at once")
    ax.set_ylim(0, max(tot) * 1.38); ax.grid(axis="x", visible=False)
    ax.legend(loc="upper left", ncols=2)
    return save(fig, "fig_memory")


def fig_ladder(ld: dict) -> str:
    arms = [("d1", "decline:\nvalue missing"), ("compute", "numeric\nanswer right"),
            ("explain", "explanation\nformat"), ("fit", "judge\nfit")]
    widths = sorted(ld)
    fig, ax = plt.subplots(figsize=(COL_W, 2.0))
    bw = 0.34
    for j, (w, col) in enumerate(zip(widths, (S1, S2))):
        means = [sum(ld[w][a]) / len(ld[w][a]) for a, _ in arms]
        xs = [i + (j - 0.5) * bw for i in range(len(arms))]
        nseed = len(ld[w][arms[0][0]])
        ax.bar(xs, means, width=bw - 0.03, color=col, edgecolor=SURFACE, linewidth=0,
               label=f"d{w} ({params_for(w) / 1e6:.1f}M){'' if nseed == 1 else f', {nseed} seeds'}")
        for i, (a, _) in enumerate(arms):
            if len(ld[w][a]) > 1:                     # individual training seeds as dots
                ax.scatter([xs[i]] * len(ld[w][a]), ld[w][a], s=9, color=INK, zorder=3, linewidth=0)
            top = max([means[i]] + ld[w][a])       # clear the highest seed dot, not just the bar
            ax.text(xs[i], top + 4.0, f"{means[i]:.0f}", ha="center", color=INK, fontsize=6.5)
    ax.set_xticks(range(len(arms)), [lbl for _, lbl in arms], color=INK2)
    ax.set_ylabel("items passed (%)"); ax.set_ylim(0, 116); ax.grid(axis="x", visible=False)
    ax.legend(loc="lower center", bbox_to_anchor=(0.5, 1.0), ncols=2)
    return save(fig, "fig_ladder")


def save(fig, name: str) -> str:
    fig.tight_layout(pad=0.3)
    p = OUT / f"{name}.pdf"
    # No CreationDate: the same data then gives the same bytes, so a rebuild shows no false change.
    fig.savefig(p, metadata={"CreationDate": None})
    if PREVIEW_ON:
        PREVIEW.mkdir(parents=True, exist_ok=True)
        fig.savefig(PREVIEW / f"{name}.png", dpi=200)
    plt.close(fig)
    return p.name


def main(out: str | None = None) -> None:
    """Draw every figure into paper/figures/, or into `out` (then without PNG previews)."""
    global OUT, PREVIEW_ON
    if out:
        OUT, PREVIEW_ON = Path(out), False
    style()
    fw, sw, ld = parse_forward(), parse_sweep(), parse_ladder()
    # fig_ladder is no longer in the paper (Table 7 carries the ladder); it is not written, so the
    # manuscript folder holds only figures the paper includes.
    made = [fig_position(fw), fig_stages(fw), fig_frontier(sw), fig_memory(sw)]
    print("wrote:", ", ".join(made))
    print(f"session: {SESSION.relative_to(ROOT)} (last bench_forward, last valid bench_sweep); "
          f"shapes timed: {', '.join(sorted(sw['timed']))}")
    print(f"ladder seeds per width: " + ", ".join(f"d{w}: {len(ld[w]['compute'])}" for w in sorted(ld)))


if __name__ == "__main__":
    import sys
    main(sys.argv[sys.argv.index("--out") + 1] if "--out" in sys.argv else None)
