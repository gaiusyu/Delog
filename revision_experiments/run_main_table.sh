#!/bin/bash
# DeLog TSC revision: version 3 (zero-padded typed fields), all Loghub datasets, paper environment.
set -u
W=$(cd "$(dirname "$0")" && pwd); cd $W
CSV=$W/results/main_table_v3_devmachine.csv
mkdir -p results; [ -f $CSV ] || echo "dataset,mode,orig_bytes,archive_bytes,cr,comp_ms,cs_mbps,decomp_ms,dcs_mbps,peak_rss_kb,lossless" > $CSV
for ds in "$@"; do
  orig=$(stat -L -c %s Logs/$ds/$ds.log); sha=$(sha256sum Logs/$ds/$ds.log | cut -d' ' -f1)
  for mode in normal fast; do
    /usr/bin/time -v -o /tmp/delog_t_$$.txt ./bin/Delog_compress $ds text 100000 4 0 lzma $mode > logs/${ds}_${mode}.comp.log 2>&1
    ms=$(grep "Total execution time" logs/${ds}_${mode}.comp.log | awk '{print $4}'); rss=$(grep "Maximum resident" /tmp/delog_t_$$.txt | awk '{print $NF}')
    arch=$(du -cb output/$ds/chunk_* | tail -1 | cut -f1)
    s=$(date +%s%N); ./bin/Delog_decompress output/$ds restored_$ds.log 4 > logs/${ds}_${mode}.dec.log 2>&1; e=$(date +%s%N); dms=$(( (e-s)/1000000 ))
    [ "$(sha256sum restored_$ds.log | cut -d' ' -f1)" = "$sha" ] && ok=yes || ok=NO
    python3 -c "o=$orig;print(f'$ds,$mode,{o},$arch,{o/$arch:.3f},$ms,{o/1048576/($ms/1000):.2f},$dms,{o/1048576/($dms/1000):.2f},$rss,$ok')" >> $CSV
    rm -rf restored_$ds.log output/$ds decompress_temp
  done
done
echo DONE >> logs/progress.txt
