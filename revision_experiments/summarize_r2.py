"""Summarize the revision re-run (r2_*.csv) into the numbers used in the paper.

Usage: python3 summarize_r2.py RESULTS_DIR
Throughput uses the timings reported by each tool (median over runs). MB means MiB.
"""
import collections
import csv
import statistics as st
import sys

ORDER = ["Android", "Apache", "BGL", "Hadoop", "HDFS", "HealthApp", "HPC", "Linux", "Mac", "OpenSSH",
         "OpenStack", "Proxifier", "Spark", "Thunderbird", "Windows", "Zookeeper"]
MB = 1048576


def load(path):
    return list(csv.DictReader(open(path)))


def main(res):
    rows = load(f"{res}/r2_main_speed.csv")
    g = collections.defaultdict(list)
    for r in rows:
        g[(r["dataset"], r["method"])].append(r)
    print("== Main table (median over runs)")
    print(f"{'dataset':12} {'method':8} {'CR':>8} {'CS':>7} {'DCS':>8} {'RSS_MB':>7} {'restored':>8} lossless")
    agg = {}
    for d in ORDER:
        for m in ["DeLog", "DeLog-L", "Denum"]:
            rs = g[(d, m)]
            if not rs:
                continue
            o = int(rs[0]["orig_bytes"])
            cr = o / st.median(int(r["archive_bytes"]) for r in rs)
            cs = st.median(o / MB / (int(r["comp_internal_ms"]) / 1000) for r in rs)
            dcs = st.median(o / MB / (int(r["decomp_internal_ms"]) / 1000) for r in rs if r["decomp_internal_ms"])
            rss = st.median(int(r["peak_rss_kb"]) for r in rs) / 1024 if rs[0]["peak_rss_kb"] != "NA" else float("nan")
            rest = st.median(int(r["restored_bytes"]) / o for r in rs)
            ok = ",".join(sorted({r["lossless"] for r in rs}))
            agg[(d, m)] = dict(cr=cr, cs=cs, dcs=dcs, rss=rss, rest=rest, ok=ok)
            print(f"{d:12} {m:8} {cr:8.2f} {cs:7.2f} {dcs:8.2f} {rss:7.0f} {rest:8.3f} {ok}")
    for m in ["DeLog", "DeLog-L", "Denum"]:
        ks = [d for d in ORDER if (d, m) in agg]
        print(f"{m:8} n={len(ks)} avgCR={st.mean(round(agg[(d, m)]['cr'], 2) for d in ks):.3f} "
              f"avgCS={st.mean(agg[(d, m)]['cs'] for d in ks):.2f} avgDCS={st.mean(agg[(d, m)]['dcs'] for d in ks):.2f}")
    full = [d for d in ORDER if agg.get((d, "Denum"), {}).get("rest", 0) > 0.9]
    print("Denum restored >90% on", len(full), "datasets:", full)
    lossless_all = all(agg[(d, m)]["ok"] == "yes" for d in ORDER for m in ["DeLog", "DeLog-L"] if (d, m) in agg)
    print("DeLog/DeLog-L all lossless:", lossless_all)

    print("\n== Decompression phases")
    ph = collections.defaultdict(lambda: [0, 0, 0])
    for r in load(f"{res}/r2_decomp_phases.csv"):
        a = ph[(r["dataset"], r["mode"])]
        a[0] += int(r["extract_us"]); a[1] += int(r["load_us"]); a[2] += int(r["rebuild_us"])
    for mode in ["normal", "fast"]:
        sh = {d: [x / sum(v) * 100 for x in v] for (d, m), v in ph.items() if m == mode}
        for i, n in enumerate(["extract", "load", "rebuild"]):
            print(f"{mode:6} {n:8} {min(s[i] for s in sh.values()):.1f}--{max(s[i] for s in sh.values()):.1f}%")
    rb = {d: (ph[(d, 'normal')][2], ph[(d, 'fast')][2]) for d, m in ph if m == "normal"}
    print("rebuild time DeLog/DeLog-L ratio:", {d: round(a / b, 2) for d, (a, b) in rb.items()})

    print("\n== Chunk sensitivity")
    ch = collections.defaultdict(dict)
    for r in load(f"{res}/r2_chunk_sensitivity.csv"):
        ch[r["dataset"]][int(r["block_lines"])] = r
    print("all lossless:", all(r["lossless"] == "yes" for v in ch.values() for r in v.values()))
    for bs in (10000, 50000, 500000):
        ch_ = [(int(v[100000]["archive_bytes"]) / int(v[bs]["archive_bytes"]) - 1) * 100 for v in ch.values()]
        print(f"{bs}: {min(ch_):+.1f}% .. {max(ch_):+.1f}%")
    sig = {d: (float(v[100000]["sig_mean"]), int(v[100000]["sig_max"]), int(v[100000]["peak_rss_kb"]) / 1024) for d, v in ch.items()}
    top = max(sig, key=lambda d: sig[d][0])
    print("most signatures:", top, sig[top])
    print("HDFS RSS 100K/500K MiB:", int(ch["HDFS"][100000]["peak_rss_kb"]) / 1024, int(ch["HDFS"][500000]["peak_rss_kb"]) / 1024)

    print("\n== Recognizer off")
    ro = load(f"{res}/r2_recognizer_off.csv")
    on_bytes = {d: st.median(int(r["archive_bytes"]) for r in g[(d, "DeLog")]) for d in ORDER}
    tot_o = sum(int(r["orig_bytes"]) for r in ro)
    off_cr = tot_o / sum(int(r["archive_bytes"]) for r in ro)
    on_cr = tot_o / sum(on_bytes[r["dataset"]] for r in ro)
    print(f"global CR on={on_cr:.2f} off={off_cr:.2f} retained={off_cr / on_cr * 100:.2f}%  lossless={all(r['lossless'] == 'yes' for r in ro)}")
    per = [int(r["orig_bytes"]) / int(r["archive_bytes"]) / (int(r["orig_bytes"]) / on_bytes[r["dataset"]]) for r in ro]
    print(f"per-dataset retained: mean {st.mean(per) * 100:.1f}% min {min(per) * 100:.1f}% max {max(per) * 100:.1f}%")

    print("\n== Anchor stress")
    for r in load(f"{res}/r2_anchor_stress.csv"):
        print(r["dataset"], r["slot"], r["variant"], round(int(r["orig_bytes"]) / int(r["archive_bytes"]), 2), r["sig_distinct"], r["sig_mean"], r["lossless"])


if __name__ == "__main__":
    main(sys.argv[1])
