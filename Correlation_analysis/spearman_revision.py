"""Spearman correlation between parsing metrics and CR (TSC revision).

Reads the parser--dataset matrix directly from the manuscript table
(empirical_full_tables.tex) so the reported numbers stay in sync with it.
Reports pooled correlations with and without the GT output, and
within-dataset correlations over the six actual parsers.

Usage: python spearman_revision.py path/to/empirical_full_tables.tex
"""
import re
import sys

import numpy as np
from scipy.stats import spearmanr

PARSERS = ["Drain", "AEL", "IPLoM", "LFA", "LogCluster", "Brain", "GT"]
METRICS = ["GA", "FGA", "PA", "RTA", "PTA", "FTA"]


def load(path):
    txt = open(path).read()
    records = []
    for block in txt.split(r"\multirow{2}{*}{Metric}")[1:]:
        datasets = re.findall(r"\\multicolumn\{7\}\{c\}\{(\w+) Raw", block)
        rows = {}
        for line in block.split("\n"):
            m = re.match(r"\s*(GA|FGA|PA|RTA|PTA|FTA|CR)\b[^&]*&(.*)\\\\", line)
            if m:
                cells = re.sub(r"\\cellcolor\{lightgray\}|\\underline", "", m.group(2)).split("&")
                rows[m.group(1)] = [float(re.sub(r"[^\d.]", "", c)) for c in cells]
        for k, ds in enumerate(datasets):
            for j, parser in enumerate(PARSERS):
                records.append((ds, parser, {name: vals[k * 7 + j] for name, vals in rows.items()}))
    return records


def main(path):
    records = load(path)
    actual = [r for r in records if r[1] != "GT"]
    datasets = sorted({r[0] for r in records})
    print(f"{len(records)} pairs incl. GT, {len(actual)} pairs excl. GT, {len(datasets)} datasets")
    print(f"{'metric':6} {'rho(all)':>9} {'rho(noGT)':>10} {'p(noGT)':>9} {'within-ds median':>17} {'within-ds range':>16}")
    for metric in METRICS:
        rho_all = spearmanr([r[2][metric] for r in records], [r[2]["CR"] for r in records])[0]
        rho, p = spearmanr([r[2][metric] for r in actual], [r[2]["CR"] for r in actual])
        within = []
        for ds in datasets:
            sub = [r for r in actual if r[0] == ds]
            within.append(spearmanr([r[2][metric] for r in sub], [r[2]["CR"] for r in sub])[0])
        within = np.array(within, dtype=float)
        print(f"{metric:6} {rho_all:9.3f} {rho:10.3f} {p:9.2g} {np.nanmedian(within):17.2f} "
              f"   [{np.nanmin(within):.2f}, {np.nanmax(within):.2f}]")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "empirical_full_tables.tex")
