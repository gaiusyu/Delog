#!/bin/bash
# Scalability (1/2/4/8 threads) and CPU time per byte for DeLog, Denum, and parallel lzma (xz -9 on
# 100K-line blocks), on public datasets. Uses the binaries and local data prepared by
# run_all_revision.sh in $L. Throughput is input MB (MiB) over wall-clock time from GNU time.
set -u
R=$(cd "$(dirname "$0")" && pwd)
L=${DELOG_WORK:-/tmp/delog_r2}
ulimit -n 1000000
OUT=$R/results/r2_scalability.csv
echo "dataset,tool,threads,orig_bytes,wall_s,user_s,sys_s" > $OUT
wall(){ awk -F': ' '/Elapsed \(wall clock\)/{n=split($2,a,":"); if(n==3) print a[1]*3600+a[2]*60+a[3]; else print a[1]*60+a[2]}' $1; }
val(){ awk -F': ' -v k="$2" '$0 ~ k {print $2}' $1; }
for ds in ${DATASETS:-Android BGL HDFS Spark Hadoop OpenSSH}; do
  orig=$(stat -L -c %s $L/Logs/$ds/$ds.log)
  for th in 1 2 4 8; do
    wd=$L/w_scal; rm -rf $wd; mkdir -p $wd/Logs; ln -sfn $L/Logs/$ds $wd/Logs/$ds; cd $wd
    /usr/bin/time -v -o t.txt $L/bin/Delog_compress $ds text 100000 $th 0 lzma normal > /dev/null 2>&1
    echo "$ds,DeLog,$th,$orig,$(wall t.txt),$(val t.txt 'User time'),$(val t.txt 'System time')" >> $OUT
    cd $L/x/denum; rm -rf Baseline
    /usr/bin/time -v -o t.txt ./compressor $ds 100000 1 $th > /dev/null 2>&1
    echo "$ds,Denum,$th,$orig,$(wall t.txt),$(val t.txt 'User time'),$(val t.txt 'System time')" >> $OUT
    rm -rf Baseline
    rm -rf $wd; mkdir -p $wd/blocks; cd $wd
    split -l 100000 $L/Logs/$ds/$ds.log blocks/b_
    /usr/bin/time -v -o t.txt sh -c "ls blocks/b_* | xargs -P $th -I{} sh -c 'xz -9 -T1 -c {} > {}.xz'"
    echo "$ds,lzma,$th,$orig,$(wall t.txt),$(val t.txt 'User time'),$(val t.txt 'System time')" >> $OUT
    rm -rf $wd
    echo "[$(date '+%m-%d %H:%M:%S')] scalability $ds $th done" >> $R/logs/r2_progress.txt
  done
done
echo "[$(date '+%m-%d %H:%M:%S')] SCALABILITY DONE" >> $R/logs/r2_progress.txt
