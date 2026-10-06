"""Plot pooled and within-dataset Spearman correlations (GT excluded) for the TSC revision.

Usage: python plot_spearman_revision.py path/to/empirical_full_tables.tex out.pdf
"""
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from scipy.stats import spearmanr

from spearman_revision import METRICS, load


def main(table_path, out_path):
    records = [r for r in load(table_path) if r[1] != "GT"]
    datasets = sorted({r[0] for r in records})
    pooled, median, lo, hi = [], [], [], []
    for metric in METRICS:
        pooled.append(spearmanr([r[2][metric] for r in records], [r[2]["CR"] for r in records])[0])
        within = []
        for ds in datasets:
            sub = [r for r in records if r[0] == ds]
            within.append(spearmanr([r[2][metric] for r in sub], [r[2]["CR"] for r in sub])[0])
        within = np.array(within, dtype=float)
        median.append(np.nanmedian(within)); lo.append(np.nanmin(within)); hi.append(np.nanmax(within))

    x = np.arange(len(METRICS)); w = 0.36
    fig, ax = plt.subplots(figsize=(6.4, 2.6))
    b1 = ax.bar(x - w / 2, pooled, w, color="white", edgecolor="black", hatch="///", label="Pooled (72 parser-dataset pairs)")
    med = np.array(median)
    b2 = ax.bar(x + w / 2, med, w, color="white", edgecolor="black", hatch="...", label="Median within dataset, range min to max")
    ax.errorbar(x + w / 2, med, yerr=[med - np.array(lo), np.array(hi) - med], fmt="none", ecolor="black", capsize=3, lw=0.8)
    for rect, v in zip(b1, pooled):
        ax.text(rect.get_x() + rect.get_width() / 2, max(v, 0) + 0.02, f"{v:.3f}", ha="center", va="bottom", fontsize=7)
    ax.axhline(0, color="black", lw=0.6)
    ax.set_xticks(x); ax.set_xticklabels(METRICS)
    ax.set_ylim(-0.6, 1.35)
    ax.set_ylabel("Spearman correlation", fontweight="bold")
    ax.set_xlabel("Parsing metric", fontweight="bold")
    ax.spines["top"].set_visible(False); ax.spines["right"].set_visible(False)
    ax.legend(fontsize=7, frameon=False, loc="upper left", ncol=2)
    fig.tight_layout()
    fig.savefig(out_path)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
