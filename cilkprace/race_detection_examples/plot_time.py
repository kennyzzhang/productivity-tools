#!/usr/bin/env python3
"""
plot_time.py - Parse and plot the output of `make time` from the
race_detection_examples build directory.

Each target's time is saved to a file like:
    <build_dir>/<test>.<variant>.time

which contains the output of bash's `time` builtin, e.g.:
    real    0m5.123s
    user    1m23.456s
    sys     0m0.789s

Usage:
    python3 plot_time.py [--build-dir PATH] [--metric real|user|sys]
                         [--output FILE] [--variants v1,v2,...] [--tests t1,t2,...]
                         [--no-plot]
"""

import argparse
import re
import sys
from pathlib import Path

try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import numpy as np
    HAS_MPL = True
except ImportError:
    HAS_MPL = False

_TIME_RE = re.compile(r"(?P<metric>real|user|sys)\s+(?P<min>\d+)m(?P<sec>[\d.]+)s")

VARIANT_COLORS = {
    "nocilk":         "#4c7bba",
    "notool":         "#6aad6a",
    "cilkprace":      "#c94040",
    "cilkprace-single": "#e07c39",
    "cilkprace-range":  "#9b6bbf",
}

VARIANT_LABELS = {
    "nocilk":         "No Cilk (serial)",
    "notool":         "Cilk (no tool)",
    "cilkprace":      "Cilkprace",
    "cilkprace-single": "Cilkprace (leb8-single)",
    "cilkprace-range":  "Cilkprace (leb8-range)",
}

# Micro/correctness-only tests excluded from the default view.
# These run in <0.01s and clutter the chart. Use --all to include them.
DEFAULT_SKIP_TESTS = {
    "blah",
    "conditional_sync",
    "fft",
    "funny_malloc",
    "heap_reuse",
    "loop",
    "memcpy_race",
    "missed_race",
    "nqueens_racy",
    "single_spawn_norace",
    "single_spawn_rr_norace",
    "single_spawn_race",
    "single_spawn_rw_race",
    "stack_reuse",
    "strdup",
}


def parse_time_file(path):
    times = {}
    try:
        text = path.read_text()
    except OSError:
        return times
    for m in _TIME_RE.finditer(text):
        secs = int(m.group("min")) * 60 + float(m.group("sec"))
        times[m.group("metric")] = secs
    return times


def collect_results(build_dir):
    results = {}
    pattern = re.compile(r"^(.+)\.([^.]+)\.time$")
    for f in sorted(build_dir.glob("*.time")):
        m = pattern.match(f.name)
        if not m:
            continue
        test, variant = m.group(1), m.group(2)
        times = parse_time_file(f)
        if not times:
            continue
        results.setdefault(test, {})[variant] = times
    return results


def print_table(results, metric, variants):
    tests = sorted(results)
    if not tests:
        print("No .time files found.")
        return
    col_w = 14
    header = f"{'benchmark':<18}" + "".join(
        f"{VARIANT_LABELS.get(v, v):>{col_w}}" for v in variants
    )
    print(header)
    print("-" * len(header))
    for test in tests:
        row = f"{test:<18}"
        for v in variants:
            val = results[test].get(v, {}).get(metric)
            if val is not None:
                row += f"{val:>{col_w-1}.2f}s"
            else:
                row += f"{'N/A':>{col_w}}"
        print(row)


def plot(results, metric, variants, output_path):
    if not HAS_MPL:
        print("matplotlib not found -- install it with: pip install matplotlib numpy",
              file=sys.stderr)
        sys.exit(1)

    tests = sorted(results)
    n_tests = len(tests)
    n_vars = len(variants)
    if n_tests == 0:
        print("Nothing to plot.")
        return

    bar_width = 0.8 / n_vars
    x = np.arange(n_tests)

    fig, ax = plt.subplots(figsize=(max(8, n_tests * 1.5), 5.5))
    fig.patch.set_facecolor("#1a1a2e")
    ax.set_facecolor("#16213e")

    for i, variant in enumerate(variants):
        values = [
            results[test].get(variant, {}).get(metric, float("nan"))
            for test in tests
        ]
        offset = (i - n_vars / 2 + 0.5) * bar_width
        color = VARIANT_COLORS.get(variant, f"C{i}")
        label = VARIANT_LABELS.get(variant, variant)
        bars = ax.bar(x + offset, values, bar_width * 0.92,
                      label=label, color=color, alpha=0.88, zorder=3)
        for bar, val in zip(bars, values):
            if val == val:  # skip NaN
                ax.text(bar.get_x() + bar.get_width() / 2,
                        bar.get_height() + 0.01,
                        f"{val:.1f}s",
                        ha="center", va="bottom",
                        fontsize=6.5, color="white", alpha=0.85)

    metric_label = {
        "real": "Wall-clock time (s)",
        "user": "User CPU time (s)",
        "sys":  "Sys CPU time (s)",
    }.get(metric, f"{metric} time (s)")

    ax.set_xticks(x)
    ax.set_xticklabels(tests, color="white", fontsize=9, rotation=20, ha="right")
    ax.tick_params(axis="y", colors="white")
    ax.set_ylabel(metric_label, color="white", fontsize=10)
    ax.set_title(f"Race Detector Benchmark  --  {metric_label}",
                 color="white", fontsize=13, fontweight="bold", pad=14)
    ax.legend(loc="upper right", framealpha=0.3, labelcolor="white",
              facecolor="#1a1a2e", edgecolor="gray", fontsize=8.5)
    ax.grid(axis="y", color="gray", alpha=0.2, zorder=0)
    for spine in ax.spines.values():
        spine.set_color("#444")

    ymin, ymax = ax.get_ylim()
    ax.set_ylim(ymin, ymax * 1.08)

    fig.tight_layout()
    fig.savefig(output_path, dpi=150, bbox_inches="tight",
                facecolor=fig.get_facecolor())
    print(f"Saved plot -> {output_path}")


def main():
    default_build = Path(__file__).parent / "build"
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build-dir", default=str(default_build), metavar="PATH",
                        help=f"Build directory with *.time files (default: {default_build})")
    parser.add_argument("--metric", choices=["real", "user", "sys"], default="real",
                        help="Time metric to plot (default: real)")
    parser.add_argument("--output", default="benchmark_plot.png", metavar="FILE",
                        help="Output image file (default: benchmark_plot.png)")
    parser.add_argument("--variants", metavar="v1,v2,...",
                        help="Comma-separated variants to include (default: all found)")
    parser.add_argument("--tests", metavar="t1,t2,...",
                        help="Comma-separated benchmarks to include (default: all found)")
    parser.add_argument("--no-plot", action="store_true",
                        help="Print table only, skip generating the plot image")
    parser.add_argument("--all", action="store_true",
                        help="Include all tests (overrides default skip list)")
    args = parser.parse_args()

    build_dir = Path(args.build_dir)
    if not build_dir.is_dir():
        print(f"Error: build directory not found: {build_dir}", file=sys.stderr)
        sys.exit(1)

    results = collect_results(build_dir)
    if not results:
        print(f"No *.time files found in {build_dir}.\n"
              "Run `make time` from the build directory first.")
        sys.exit(0)

    if args.tests:
        keep = set(args.tests.split(","))
        results = {t: v for t, v in results.items() if t in keep}
    elif not args.all:
        results = {t: v for t, v in results.items() if t not in DEFAULT_SKIP_TESTS}

    canonical = ["notool", "cilkprace-single", "cilkprace-range"]
    found_variants = {v for d in results.values() for v in d}
    if args.variants:
        variants = [v for v in args.variants.split(",") if v in found_variants]
    else:
        variants = [v for v in canonical if v in found_variants]

    print_table(results, args.metric, variants)
    if not args.no_plot:
        plot(results, args.metric, variants, args.output)


if __name__ == "__main__":
    main()
