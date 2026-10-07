#!/bin/bash
# Re-runs every experiment of the TSC revision with the released implementation.
# Layout expected in the directory of this script ($R):
#   src/{compressor.cpp,decompressor.cpp,BS_thread_pool.hpp,Denum.cpp,Denum_decompress.cpp,
#        compressor_setting1.cpp,compressor_setting2.cpp,compressor_stress.cpp,decompressor_timed.cpp}
#   Logs/<dataset>/<dataset>.log   (inputs, may be symlinks)
# Work happens on local disk in $L. Small CSV results go to $R/results/r2_*.csv.
set -u
R=$(cd "$(dirname "$0")" && pwd)
L=${DELOG_WORK:-/tmp/delog_r2}
RES=$R/results; LOG=$R/logs; mkdir -p $RES $LOG $L/bin $L/Logs
ulimit -n 1000000 2>/dev/null || ulimit -n 1000000
say(){ echo "[$(date '+%m-%d %H:%M:%S')] $*" | tee -a $LOG/r2_progress.txt; }
ms(){ echo $(( ($(date +%s%N)-$1)/1000000 )); }
ALL="Apache Linux Proxifier Mac Zookeeper HealthApp HPC Hadoop OpenStack OpenSSH Android BGL HDFS Spark Windows Thunderbird"
MID="Apache Linux Proxifier Mac Zookeeper HealthApp HPC Hadoop OpenStack OpenSSH Android BGL HDFS"

stage_build(){
  say "build start"
  cd $R/src
  g++ -std=c++17 -O3 -I. -o $L/bin/Delog_compress compressor.cpp -lpcre2-8 -lstdc++fs -pthread -larchive || return 1
  g++ -std=c++17 -O2 -o $L/bin/Delog_decompress decompressor.cpp -lstdc++fs -pthread -larchive || return 1
  g++ -std=c++17 -O2 -o $L/bin/dec_timed decompressor_timed.cpp -lstdc++fs -pthread -larchive || return 1
  for v in setting1 setting2 stress; do g++ -std=c++17 -O3 -I. -o $L/bin/comp_$v compressor_$v.cpp -lpcre2-8 -lstdc++fs -pthread -larchive || return 1; done
  mkdir -p $L/x/denum
  g++ -std=c++17 -O3 -o $L/x/denum/compressor Denum.cpp -lpcre2-8 -pthread || return 1
  g++ -std=c++17 -O3 -o $L/x/denum/decompressor Denum_decompress.cpp -pthread -larchive || return 1
  cp $L/bin/Delog_compress $L/bin/Delog_decompress $R/bin/ 2>/dev/null
  say "build done"
}

stage_data(){
  say "copy data start"
  for ds in $ALL; do mkdir -p $L/Logs/$ds; [ -s $L/Logs/$ds/$ds.log ] || cp -L $R/Logs/$ds/$ds.log $L/Logs/$ds/$ds.log; done
  for ds in $ALL; do mkdir -p $L/Logs/NR_$ds; ln -sf $L/Logs/$ds/$ds.log $L/Logs/NR_$ds/NR_$ds.log; done
  say "copy data done"
}

# run_delog <bin> <logname> <block> <mode> <workdir> -> sets ARCH CI RSS OK ; keeps output dir until cleanup
run_delog(){
  local bin=$1 name=$2 bs=$3 mode=$4 wd=$5
  mkdir -p $wd/Logs; ln -sfn $L/Logs/$name $wd/Logs/$name; cd $wd
  /usr/bin/time -v -o $wd/t.txt $bin $name text $bs 4 0 lzma $mode > $wd/c.log 2>&1
  CI=$(grep "Total execution time" $wd/c.log | awk '{print $4}'); RSS=$(grep "Maximum resident" $wd/t.txt | awk '{print $NF}')
  ARCH=$(du -cb output/$name/chunk_* | tail -1 | cut -f1)
}
sig_stats(){ # <outdir> -> SIGU (distinct signatures over chunks) SIGM (mean per chunk) SIGX (max per chunk)
  local d=$1; rm -f $d/../sig_all.txt $d/../sig_n.txt
  for f in $d/chunk_*.tar.xz; do tar -xJOf $f tags_mapping.txt | cut -d: -f2- | tee -a $d/../sig_all.txt | wc -l >> $d/../sig_n.txt; done
  SIGU=$(sort -u $d/../sig_all.txt | wc -l); read SIGM SIGX < <(awk '{s+=$1; if($1>m)m=$1} END {printf "%.1f %d\n", s/NR, m}' $d/../sig_n.txt)
}
check_restore(){ # <decomp bin> <outdir> <orig> <wd> -> DI (internal ms) RB OK
  local dbin=$1 od=$2 orig=$3 wd=$4
  $dbin $od $wd/r.log 4 > $wd/d.log 2>&1
  DI=$(grep -o "Total execution time: [0-9]*" $wd/d.log | awk '{print $4}'); RB=$(stat -c %s $wd/r.log 2>/dev/null || echo 0)
  cmp -s $wd/r.log $orig && OK=yes || OK=NO; rm -f $wd/r.log; rm -rf $wd/decompress_temp
}

stage_smoke(){
  say "smoke start"
  local wd=$L/w_smoke; rm -rf $wd; mkdir -p $wd
  for ds in Apache Linux Mac HealthApp; do
    for b in Delog_compress comp_setting1 comp_setting2; do
      run_delog $L/bin/$b $ds 100000 normal $wd; check_restore $L/bin/Delog_decompress output/$ds $L/Logs/$ds/$ds.log $wd
      say "smoke $ds $b CR=$(python3 -c "print(round($(stat -L -c %s $L/Logs/$ds/$ds.log)/$ARCH,3))") lossless=$OK"; rm -rf $wd/output
    done
    run_delog $L/bin/Delog_compress $ds 100000 fast $wd; check_restore $L/bin/Delog_decompress output/$ds $L/Logs/$ds/$ds.log $wd; say "smoke $ds fast lossless=$OK"; rm -rf $wd/output
  done
  export DELOG_STRESS_AFTER=user; run_delog $L/bin/comp_stress Linux 100000 normal $wd; unset DELOG_STRESS_AFTER; check_restore $L/bin/Delog_decompress output/Linux $L/Logs/Linux/Linux.log $wd; say "smoke stress Linux lossless=$OK"; rm -rf $wd/output
  say "smoke done"
}

stage_speed(){
  say "speed start"
  local OUT=$RES/r2_main_speed.csv wd=$L/w_speed; rm -rf $wd; mkdir -p $wd
  echo "dataset,method,run,orig_bytes,archive_bytes,comp_internal_ms,decomp_internal_ms,peak_rss_kb,restored_bytes,lossless,load1" > $OUT
  for ds in $ALL; do
    local orig=$(stat -L -c %s $L/Logs/$ds/$ds.log) runs=3; [ $orig -gt 1000000000 ] && runs=1
    for run in $(seq 1 $runs); do
      for m in normal fast; do
        run_delog $L/bin/Delog_compress $ds 100000 $m $wd; check_restore $L/bin/Delog_decompress output/$ds $L/Logs/$ds/$ds.log $wd
        local name=DeLog; [ $m = fast ] && name=DeLog-L
        echo "$ds,$name,$run,$orig,$ARCH,$CI,$DI,$RSS,$RB,$OK,$(cut -d' ' -f1 /proc/loadavg)" >> $OUT; rm -rf $wd/output
      done
      cd $L/x/denum; rm -rf Baseline
      ./compressor $ds 100000 1 4 > c.log 2>&1
      local ci=$(grep "completed in" c.log | awk '{print $(NF-1)}') arch=$(du -cb Baseline/Denum/results/$ds/*.tar.xz 2>/dev/null | tail -1 | cut -f1)
      ./decompressor Baseline/Denum/results/$ds r.log 4 > d.log 2>&1
      local di=$(grep "Total execution time" d.log | awk '{print $4}') rb=$(stat -c %s r.log 2>/dev/null || echo 0) ok=NO
      cmp -s r.log $L/Logs/$ds/$ds.log && ok=yes
      echo "$ds,Denum,$run,$orig,$arch,$ci,$di,NA,$rb,$ok,$(cut -d' ' -f1 /proc/loadavg)" >> $OUT
      rm -rf r.log Baseline denum_decompress_temp
    done
    say "speed $ds done"
  done
  say "speed done"
}

stage_phases(){
  say "phases start"
  local OUT=$RES/r2_decomp_phases.csv wd=$L/w_phase; rm -rf $wd; mkdir -p $wd
  echo "dataset,mode,run,extract_us,load_us,rebuild_us,decomp_internal_ms,lossless" > $OUT
  for ds in HealthApp Mac OpenStack OpenSSH Android BGL HDFS; do
    for m in normal fast; do
      run_delog $L/bin/Delog_compress $ds 100000 $m $wd
      for run in 1 2 3; do
        cd $wd; $L/bin/dec_timed output/$ds r.log 4 > d.log 2> ph.txt
        local p=$(grep PHASES_US ph.txt | sed 's/.*extract=\([0-9]*\) load=\([0-9]*\) rebuild=\([0-9]*\)/\1,\2,\3/')
        local di=$(grep -o "Total execution time: [0-9]*" d.log | awk '{print $4}') ok=NO
        cmp -s r.log $L/Logs/$ds/$ds.log && ok=yes; rm -rf r.log decompress_temp
        echo "$ds,$m,$run,$p,$di,$ok" >> $OUT
      done
      rm -rf $wd/output
    done
  done
  say "phases done"
}

lane_chunks(){
  local OUT=$RES/r2_chunk_sensitivity.csv wd=$L/w_chunk; rm -rf $wd; mkdir -p $wd
  echo "dataset,block_lines,orig_bytes,archive_bytes,peak_rss_kb,num_chunks,sig_mean,sig_max,lossless" > $OUT
  for ds in $MID; do
    for bs in 10000 50000 100000 500000; do
      run_delog $L/bin/Delog_compress $ds $bs normal $wd
      local n=$(ls $wd/output/$ds/chunk_* | wc -l) sm=NA sx=NA
      if [ $bs = 100000 ]; then sig_stats $wd/output/$ds; sm=$SIGM; sx=$SIGX; fi
      check_restore $L/bin/Delog_decompress output/$ds $L/Logs/$ds/$ds.log $wd
      echo "$ds,$bs,$(stat -L -c %s $L/Logs/$ds/$ds.log),$ARCH,$RSS,$n,$sm,$sx,$OK" >> $OUT; rm -rf $wd/output
    done
    say "chunk $ds done"
  done
}

lane_cr_variants(){
  local wd=$L/w_var; rm -rf $wd; mkdir -p $wd
  local O1=$RES/r2_recognizer_off.csv
  echo "dataset,orig_bytes,archive_bytes,lossless" > $O1
  for ds in $ALL; do
    run_delog $L/bin/Delog_compress NR_$ds 100000 normal $wd; check_restore $L/bin/Delog_decompress output/NR_$ds $L/Logs/$ds/$ds.log $wd
    echo "$ds,$(stat -L -c %s $L/Logs/$ds/$ds.log),$ARCH,$OK" >> $O1; rm -rf $wd/output; say "recognizer-off $ds done"
  done
  local O2=$RES/r2_ablation.csv
  echo "dataset,setting,orig_bytes,archive_bytes,lossless" > $O2
  for ds in Zookeeper HDFS OpenSSH BGL HealthApp; do
    for st in setting1 setting2; do
      run_delog $L/bin/comp_$st $ds 100000 normal $wd; check_restore $L/bin/Delog_decompress output/$ds $L/Logs/$ds/$ds.log $wd
      echo "$ds,$st,$(stat -L -c %s $L/Logs/$ds/$ds.log),$ARCH,$OK" >> $O2; rm -rf $wd/output
    done
    say "ablation $ds done"
  done
  lane_stress
}
lane_stress(){
  local wd=$L/w_stress; rm -rf $wd; mkdir -p $wd
  local O3=$RES/r2_anchor_stress.csv
  echo "dataset,slot,variant,orig_bytes,archive_bytes,sig_distinct,sig_mean,lossless" > $O3
  for spec in "OpenSSH|user|AFTER" "Linux|user|AFTER" "Android|I,D|TOKENS" "Hadoop|INFO|TOKENS"; do
    IFS='|' read ds slot kind <<< "$spec"
    for variant in default stress; do
      if [ $variant = default ]; then run_delog $L/bin/Delog_compress $ds 100000 normal $wd
      elif [ $kind = AFTER ]; then export DELOG_STRESS_AFTER=$slot; run_delog $L/bin/comp_stress $ds 100000 normal $wd; unset DELOG_STRESS_AFTER
      else export DELOG_STRESS_TOKENS=$slot; run_delog $L/bin/comp_stress $ds 100000 normal $wd; unset DELOG_STRESS_TOKENS; fi
      sig_stats $wd/output/$ds; check_restore $L/bin/Delog_decompress output/$ds $L/Logs/$ds/$ds.log $wd
      echo "$ds,\"$slot\",$variant,$(stat -L -c %s $L/Logs/$ds/$ds.log),$ARCH,$SIGU,$SIGM,$OK" >> $O3; rm -rf $wd/output
    done
    say "stress $ds done"
  done
}

case "${1:-all}" in
  all)
    stage_build || { say "BUILD FAILED"; exit 1; }
    stage_data; stage_smoke; stage_speed; stage_phases
    say "parallel lanes start"
    lane_chunks & lane_cr_variants & wait
    say "ALL DONE" ;;
  *) "$@" ;;
esac
