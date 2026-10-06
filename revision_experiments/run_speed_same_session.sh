#!/bin/bash
# Same-session throughput v2: internal timings reported by each tool, plus restored size. Local NVMe, 100K-line blocks, 4 threads.
R=$(cd "$(dirname "$0")" && pwd)
L=/tmp/delog_speed2; mkdir -p $L/Logs $L/x/denum $L/bin
cp $R/bin/Delog_compress $R/bin/Delog_decompress $L/bin/
g++ -std=c++17 -O3 -o $L/x/denum/compressor $R/../Baselines/Denum/Denum.cpp -lpcre2-8 -pthread || exit 1
g++ -std=c++17 -O3 -o $L/x/denum/decompressor $R/../Baselines/Denum/Denum_decompress.cpp -pthread -larchive || exit 1
OUT=$R/results/speed_same_session_v2.csv
[ -f $OUT ] || echo "dataset,method,run,orig_bytes,archive_bytes,comp_internal_ms,decomp_internal_ms,decomp_reported_mbps,restored_bytes,lossless,load1" > $OUT
for ds in "$@"; do
  mkdir -p $L/Logs/$ds; cp -L $R/Logs/$ds/$ds.log $L/Logs/$ds/$ds.log
  orig=$(stat -c %s $L/Logs/$ds/$ds.log); runs=3; [ $orig -gt 1000000000 ] && runs=1
  for run in $(seq 1 $runs); do
    for m in normal fast; do
      cd $L; ./bin/Delog_compress $ds text 100000 4 0 lzma $m > c.log 2>&1
      ci=$(grep "Total execution time" c.log | awk '{print $4}'); arch=$(du -cb output/$ds/chunk_* | tail -1 | cut -f1)
      ./bin/Delog_decompress output/$ds r.log 4 > d.log 2>&1
      di=$(grep -i "Total execution time\|Decompression time\|took" d.log | grep -o "[0-9]\+ ms" | head -1 | awk '{print $1}'); dr=$(grep -i "Decompression speed" d.log | grep -o "[0-9.]\+" | head -1)
      rb=$(stat -c %s r.log 2>/dev/null || echo 0); cmp -s r.log Logs/$ds/$ds.log && ok=yes || ok=NO
      name=DeLog; [ $m = fast ] && name=DeLog-L
      echo "$ds,$name,$run,$orig,$arch,$ci,$di,$dr,$rb,$ok,$(cut -d' ' -f1 /proc/loadavg)" >> $OUT
      [ $run = 1 ] && cp d.log $R/logs/dec_${ds}_${name}.log
      rm -rf r.log output/$ds decompress_temp c.log d.log
    done
    cd $L/x/denum; ./compressor $ds 100000 1 4 > c.log 2>&1
    ci=$(grep "completed in" c.log | awk '{print $(NF-1)}'); arch=$(du -cb Baseline/Denum/results/$ds/*.tar.xz 2>/dev/null | tail -1 | cut -f1)
    ./decompressor Baseline/Denum/results/$ds r.log 4 > d.log 2>&1
    di=$(grep "Total execution time" d.log | awk '{print $4}'); dr=$(grep "Decompression Speed" d.log | grep -o "[0-9.]\+" | head -1)
    rb=$(stat -c %s r.log 2>/dev/null || echo 0); cmp -s r.log $L/Logs/$ds/$ds.log && ok=yes || ok=NO
    echo "$ds,Denum,$run,$orig,$arch,$ci,$di,$dr,$rb,$ok,$(cut -d' ' -f1 /proc/loadavg)" >> $OUT
    [ $run = 1 ] && cp d.log $R/logs/dec_${ds}_Denum.log
    rm -rf r.log Baseline denum_decompress_temp c.log d.log
  done
  rm -rf $L/Logs/$ds
done
echo ALLDONE >> $R/logs/speed2.done
