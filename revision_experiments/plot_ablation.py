"""Plot the feature ablation (Settings 1-3) from r2_ablation.csv and r2_main_speed.csv.

Usage: python3 plot_ablation.py RESULTS_DIR OUT.pdf
Setting 3 is the full DeLog (normal mode, run 1 of the main table run).
"""
import csv
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

DATASETS = ["Zookeeper", "HDFS", "OpenSSH", "BGL", "HealthApp"]


def main(res, out):
    cr = {}
    for r in csv.DictReader(open(f"{res}/r2_ablation.csv")):
        cr[(r["dataset"], r["setting"])] = int(r["orig_bytes"]) / int(r["archive_bytes"])
    for r in csv.DictReader(open(f"{res}/r2_main_speed.csv")):
        if r["method"] == "DeLog" and r["run"] == "1" and r["dataset"] in DATASETS:
            cr[(r["dataset"], "setting3")] = int(r["orig_bytes"]) / int(r["archive_bytes"])

    plt.rcParams.update({"font.family": "serif", "font.size": 13})
    x = np.arange(len(DATASETS)); w = 0.26
    styles = [("setting1", "Setting 1", "#4C72B0", "////"), ("setting2", "Setting 2", "#DD8452", "...."), ("setting3", "Setting 3", "#55A868", "xxxx")]
    fig, ax = plt.subplots(figsize=(8.4, 3.1))
    for i, (key, label, color, hatch) in enumerate(styles):
        vals = [cr[(d, key)] for d in DATASETS]
        bars = ax.bar(x + (i - 1) * w, vals, w, label=label, color=color, edgecolor="black", hatch=hatch, linewidth=0.8)
        for b, v in zip(bars, vals):
            ax.text(b.get_x() + b.get_width() / 2, v + 3, f"{v:.1f}", ha="center", va="bottom", rotation=90, fontsize=10, fontweight="bold")
    ax.set_xticks(x); ax.set_xticklabels(DATASETS)
    ax.set_ylabel("Compression Ratio", fontweight="bold", fontsize=15)
    ax.set_ylim(0, max(cr.values()) * 1.35)
    ax.yaxis.grid(True, linestyle="--", alpha=0.6); ax.set_axisbelow(True)
    ax.spines["top"].set_visible(False); ax.spines["right"].set_visible(False)
    ax.legend(ncol=3, loc="upper center", bbox_to_anchor=(0.5, 1.22), frameon=False)
    fig.tight_layout()
    fig.savefig(out, bbox_inches="tight")
    for d in DATASETS:
        print(d, " ".join(f"{cr[(d, k)]:.2f}" for k, *_ in styles))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
