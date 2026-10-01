#!/bin/bash
# TSC revision experiments, run inside the delog-exp container.
#   Exp A (R2.6): chunk-size sensitivity -- CR + SHA-256 round trip for several block sizes.
#   Exp B (R1.1): peak RSS and signatures per chunk (block size 100K).
#   Exp C (R1.2): decompression time split -- lzma/tar extraction only vs. full decompression.
# Usage: run_revision_exp.sh "<datasets>" "<block sizes>" <threads>
set -u
DATASETS=${1:-"Apache Linux"}
BLOCKS=${2:-"10000 50000 100000 500000"}
THREADS=${3:-4}
W=$(cd "$(dirname "$0")" && pwd); RES=$W/results
mkdir -p $RES
CSV=$RES/chunk_sensitivity_v3.csv
[ -f $CSV ] || echo "dataset,block_lines,threads,orig_bytes,archive_bytes,cr,comp_ms,peak_rss_kb,num_chunks,sig_mean,sig_max,decomp_ms,untar_ms,lossless" > $CSV

for ds in $DATASETS; do
  true
  orig=$(stat -L -c %s $W/Logs/$ds/$ds.log)
  src_sha=$(sha256sum $W/Logs/$ds/$ds.log | cut -d' ' -f1)
  for bs in $BLOCKS; do
    cd $W
    /usr/bin/time -v -o /tmp/delog_ct_$$.txt ./bin/Delog_compress $ds text $bs $THREADS 0 lzma normal > /tmp/delog_comp_$$.log 2>&1
    comp_ms=$(grep "Total execution time" /tmp/delog_comp_$$.log | awk '{print $4}')
    rss=$(grep "Maximum resident" /tmp/delog_ct_$$.txt | awk '{print $NF}')
    arch=$(du -cb output/$ds/chunk_* | tail -1 | cut -f1)
    nch=$(ls output/$ds/chunk_* | wc -l)
    cr=$(python3 -c "print(round($orig/$arch,3))")
    sig_mean=NA; sig_max=NA
    if [ "$bs" = "100000" ]; then
      for f in output/$ds/chunk_*.tar.xz; do tar -xJOf $f tags_mapping.txt | wc -l; done > /tmp/delog_sigs_$$.txt
      read sig_mean sig_max < <(python3 -c "v=[int(x) for x in open('/tmp/delog_sigs_$$.txt')];print(round(sum(v)/len(v),1),max(v))")
    fi
    # full decompression
    s=$(date +%s%N); ./bin/Delog_decompress output/$ds /tmp/delog_rest_$$.log $THREADS > /tmp/delog_dec_$$.log 2>&1; e=$(date +%s%N)
    dec_ms=$(( (e-s)/1000000 ))
    if [ "$(sha256sum /tmp/delog_rest_$$.log | cut -d' ' -f1)" = "$src_sha" ]; then ok=yes; else ok=NO; fi
    rm -f /tmp/delog_rest_$$.log
    # extraction only (lzma decode + tar unpack), same parallelism
    rm -rf /tmp/delog_untar_$$ && mkdir -p /tmp/delog_untar_$$
    s=$(date +%s%N)
    ls output/$ds/chunk_*.tar.xz | xargs -P $THREADS -I{} sh -c 'd=/tmp/delog_untar_$$/$(basename {} .tar.xz); mkdir -p $d; tar -xJf {} -C $d'
    e=$(date +%s%N); untar_ms=$(( (e-s)/1000000 ))
    rm -rf /tmp/delog_untar_$$ decompress_temp
    echo "$ds,$bs,$THREADS,$orig,$arch,$cr,$comp_ms,$rss,$nch,$sig_mean,$sig_max,$dec_ms,$untar_ms,$ok" | tee -a $CSV
    rm -rf output/$ds
  done
done
