"""Plot compression scalability and CPU cycles per byte from r2_scalability.csv.

Usage: python3 plot_scalability_cpu.py r2_scalability.csv SCAL_OUT.pdf CPU_OUT.pdf [CPU_HZ]
Throughput = input MiB / wall-clock seconds. CPU cycles per byte = (user + system CPU seconds)
x nominal clock frequency (default 2.3 GHz, Xeon Platinum 8336C) / input bytes, at 8 threads.
"""
import collections
import csv
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

TOOLS = [("DeLog", "#d62728", "o", "////"), ("Denum", "#1f77b4", "s", "...."), ("lzma", "#2ca02c", "^", "xxxx")]
LABEL = {"DeLog": "DeLog", "Denum": "Denum", "lzma": "LZMA"}


def main(path, scal_out, cpu_out, hz=2.3e9):
    rows = list(csv.DictReader(open(path)))
    data = collections.defaultdict(dict)
    datasets = []
    for r in rows:
        if r["dataset"] not in datasets:
            datasets.append(r["dataset"])
        orig = int(r["orig_bytes"])
        thr = orig / 1048576 / float(r["wall_s"])
        cpb = (float(r["user_s"]) + float(r["sys_s"])) * hz / orig
        data[(r["dataset"], r["tool"])][int(r["threads"])] = (thr, cpb)

    plt.rcParams.update({"font.family": "serif", "font.size": 11})
    ncol = 3
    nrow = (len(datasets) + ncol - 1) // ncol
    fig, axes = plt.subplots(nrow, ncol, figsize=(10, 3.0 * nrow), squeeze=False)
    for i, ds in enumerate(datasets):
        ax = axes[i // ncol][i % ncol]
        for tool, color, marker, _ in TOOLS:
            pts = data[(ds, tool)]
            th = sorted(pts)
            ax.plot(th, [pts[t][0] for t in th], color=color, marker=marker, markerfacecolor="none", linewidth=2, label=LABEL[tool])
        ax.set_xscale("log", base=2)
        ax.set_xticks([1, 2, 4, 8]); ax.set_xticklabels(["1", "2", "4", "8"])
        ax.set_title(ds, fontweight="bold")
        ax.set_ylim(bottom=0)
        ax.yaxis.grid(True, linestyle="--", alpha=0.6)
        if i % ncol == 0:
            ax.set_ylabel("Throughput (MB/s)", fontweight="bold")
        if i // ncol == nrow - 1:
            ax.set_xlabel("Thread Count", fontweight="bold")
    for j in range(len(datasets), nrow * ncol):
        axes[j // ncol][j % ncol].axis("off")
    handles, labels = axes[0][0].get_legend_handles_labels()
    fig.legend(handles, labels, ncol=3, loc="upper center", bbox_to_anchor=(0.5, 1.03), frameon=False)
    fig.tight_layout()
    fig.savefig(scal_out, bbox_inches="tight")

    fig, ax = plt.subplots(figsize=(8.0, 2.8))
    x = np.arange(len(datasets)); w = 0.26
    for k, (tool, color, _, hatch) in enumerate(TOOLS):
        vals = [data[(ds, tool)][8][1] for ds in datasets]
        bars = ax.bar(x + (k - 1) * w, vals, w, color=color, hatch=hatch, edgecolor="black", linewidth=0.6, label=LABEL[tool], alpha=0.85)
        for b, v in zip(bars, vals):
            ax.text(b.get_x() + b.get_width() / 2, v + 10, f"{v:.0f}", ha="center", va="bottom", fontsize=8)
    ax.set_xticks(x); ax.set_xticklabels(datasets)
    ax.set_ylabel("CPU Cycles per Byte\n(Lower is Better)", fontweight="bold")
    ax.yaxis.grid(True, linestyle="--", alpha=0.6); ax.set_axisbelow(True)
    ax.legend(ncol=3, loc="upper center", bbox_to_anchor=(0.5, 1.2), frameon=False)
    fig.tight_layout()
    fig.savefig(cpu_out, bbox_inches="tight")
    for ds in datasets:
        print(ds, {t: [round(data[(ds, t)][th][0], 1) for th in (1, 2, 4, 8)] for t, *_ in TOOLS},
              {t: round(data[(ds, t)][8][1]) for t, *_ in TOOLS})


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], sys.argv[3], float(sys.argv[4]) if len(sys.argv) > 4 else 2.3e9)
