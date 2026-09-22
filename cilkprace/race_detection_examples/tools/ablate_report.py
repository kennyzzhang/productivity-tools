#!/usr/bin/env python3
"""Turn an ablate.py sweep CSV into a table (and optionally a chart).

Each non-baseline config has one optimization removed, so its slowdown relative
to baseline is what that optimization buys:

    effect(knob) = time(knob removed) / time(baseline) - 1

Positive means the optimization is earning its keep. Around zero means removing
it costs nothing measurable. Negative means it is making things slower.

Per-benchmark ratios are combined with a geometric mean, which is the correct
average for ratios.

Usage:
    tools/ablate_report.py ablation-results/sweep.csv
    tools/ablate_report.py ablation-results/sweep.csv --plot ablation.png
"""

import argparse
import csv
import math
import statistics
import sys
from collections import defaultdict
from pathlib import Path

BASELINE = "baseline"


def load(path):
    """-> {(config, benchmark, workers): median_seconds}, plus notes."""
    runs = defaultdict(list)
    notes = defaultdict(set)
    with open(path, newline="") as fh:
        for row in csv.DictReader(fh):
            key = (row["config"], row["benchmark"], int(row["workers"]))
            if row["seconds"]:
                runs[key].append(float(row["seconds"]))
            elif row["note"]:
                notes[key].add(row["note"])
    return {k: statistics.median(v) for k, v in runs.items()}, notes


def geomean(xs):
    return math.exp(sum(math.log(x) for x in xs) / len(xs)) if xs else float("nan")


def summarize(med, configs, benchmarks, worker_counts):
    """-> {(config, workers): (pct_effect, n_benchmarks)}"""
    out = {}
    for cfg in configs:
        if cfg == BASELINE:
            continue
        for w in worker_counts:
            ratios = []
            for b in benchmarks:
                base = med.get((BASELINE, b, w))
                this = med.get((cfg, b, w))
                if base and this:
                    ratios.append(this / base)
            out[(cfg, w)] = ((geomean(ratios) - 1) * 100 if ratios else float("nan"),
                             len(ratios))
    return out


def print_summary(summary, configs, worker_counts, notes_by_cfg):
    cfgs = [c for c in configs if c != BASELINE]
    order = sorted(cfgs, key=lambda c: -(summary.get((c, worker_counts[0]), (0,))[0] or 0))

    wcols = "".join(f"{'w=' + str(w):>14}" for w in worker_counts)
    header = f"{'optimization removed':<26}{wcols}   coverage"
    print("\nEffect of removing each optimization (geomean over benchmarks)")
    print("positive = the optimization is helping by that much\n")
    print(header)
    print("-" * len(header))
    for cfg in order:
        row = f"{cfg.replace('no-', ''):<26}"
        cov = []
        for w in worker_counts:
            pct, n = summary.get((cfg, w), (float("nan"), 0))
            row += f"{'n/a':>14}" if n == 0 else f"{pct:>+13.1f}%"
            cov.append(str(n))
        row += f"   {'/'.join(cov)} bench"
        print(row)
        for note in sorted(notes_by_cfg.get(cfg, [])):
            print(f"{'':<26}   ! {note}")


def print_detail(med, configs, benchmarks, w):
    cfgs = [c for c in configs if c != BASELINE]
    print(f"\n\nPer-benchmark effect at {w} worker(s), % vs baseline\n")
    head = f"{'benchmark':<12}{'baseline':>10}" + "".join(
        f"{c.replace('no-', '')[:11]:>12}" for c in cfgs)
    print(head)
    print("-" * len(head))
    for b in benchmarks:
        base = med.get((BASELINE, b, w))
        if not base:
            continue
        row = f"{b:<12}{base:>9.3f}s"
        for c in cfgs:
            this = med.get((c, b, w))
            row += f"{'-':>12}" if not this else f"{(this / base - 1) * 100:>+11.1f}%"
        print(row)


def plot(summary, configs, worker_counts, out_path):
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        import numpy as np
    except ImportError:
        sys.exit("matplotlib/numpy needed for --plot")

    # Categorical slots 1 and 2, dark-mode steps, from the reference palette.
    SERIES = ["#3987e5", "#d95926"]
    SURFACE, INK, INK_DIM = "#1a1a19", "#ffffff", "#c3c2b7"

    cfgs = [c for c in configs if c != BASELINE]
    order = sorted(cfgs, key=lambda c: summary.get((c, worker_counts[0]), (0,))[0] or 0)
    labels = [c.replace("no-", "") for c in order]

    y = np.arange(len(order))
    h = 0.8 / len(worker_counts)

    fig, ax = plt.subplots(figsize=(9, max(3.5, 0.52 * len(order) + 1.6)))
    fig.patch.set_facecolor(SURFACE)
    ax.set_facecolor(SURFACE)

    for i, w in enumerate(worker_counts):
        vals = [summary.get((c, w), (float("nan"),))[0] for c in order]
        offset = (i - len(worker_counts) / 2 + 0.5) * h
        bars = ax.barh(y + offset, vals, h * 0.88, label=f"{w} worker" + ("s" if w > 1 else ""),
                       color=SERIES[i % len(SERIES)], zorder=3)
        for bar, v in zip(bars, vals):
            if v == v:
                ax.text(bar.get_width() + (0.35 if v >= 0 else -0.35),
                        bar.get_y() + bar.get_height() / 2, f"{v:+.1f}%",
                        va="center", ha="left" if v >= 0 else "right",
                        fontsize=8, color=INK_DIM)

    ax.axvline(0, color=INK_DIM, lw=1, zorder=4)
    ax.set_yticks(y)
    ax.set_yticklabels(labels, color=INK, fontsize=9)
    ax.tick_params(axis="x", colors=INK_DIM)
    ax.set_xlabel("slowdown when the optimization is removed (%)", color=INK_DIM, fontsize=9)
    ax.set_title("cilkprace leb8-single: what each optimization buys",
                 color=INK, fontsize=12, fontweight="bold", loc="left", pad=12)
    ax.grid(axis="x", color=INK_DIM, alpha=0.15, zorder=0)
    for side in ("top", "right", "left"):
        ax.spines[side].set_visible(False)
    ax.spines["bottom"].set_color("#44443f")
    leg = ax.legend(loc="lower right", frameon=False, fontsize=9)
    for t in leg.get_texts():
        t.set_color(INK)

    xmin, xmax = ax.get_xlim()
    ax.set_xlim(xmin - abs(xmin) * 0.12 - 1, xmax + abs(xmax) * 0.12 + 1)
    fig.tight_layout()
    fig.savefig(out_path, dpi=150, facecolor=SURFACE, bbox_inches="tight")
    print(f"\nwrote {out_path}")


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("csv")
    p.add_argument("--plot", metavar="PNG", help="also write a chart")
    p.add_argument("--detail", action="store_true", help="per-benchmark tables too")
    args = p.parse_args()

    med, notes = load(args.csv)
    if not med:
        sys.exit(f"no usable timings in {args.csv}")

    configs, benchmarks, worker_counts = [], [], []
    for cfg, b, w in med:
        if cfg not in configs: configs.append(cfg)
        if b not in benchmarks: benchmarks.append(b)
        if w not in worker_counts: worker_counts.append(w)
    for cfg, b, w in notes:
        if cfg not in configs: configs.append(cfg)
        if w not in worker_counts: worker_counts.append(w)
    worker_counts.sort()
    benchmarks.sort()
    if BASELINE not in configs:
        sys.exit("no baseline config in the CSV")

    notes_by_cfg = defaultdict(set)
    for (cfg, b, w), ns in notes.items():
        for n in ns:
            notes_by_cfg[cfg].add(f"{b} @ w={w}: {n}")

    summary = summarize(med, configs, benchmarks, worker_counts)
    print_summary(summary, configs, worker_counts, notes_by_cfg)
    if args.detail:
        for w in worker_counts:
            print_detail(med, configs, benchmarks, w)
    if args.plot:
        plot(summary, configs, worker_counts, args.plot)


if __name__ == "__main__":
    main()
