#!/bin/bash
R=$(cd "$(dirname "$0")" && pwd)
L=/tmp/delog_phase_$$; mkdir -p $L/bin; cp $R/bin/Delog_compress $L/bin/; cd $L
g++ -std=c++17 -O2 -o bin/dec_timed $R/decompressor_timed.cpp -lstdc++fs -pthread -larchive || exit 1
echo "dataset,run,wall_ms,extract_us,load_us,rebuild_us,lossless"
for ds in HealthApp Mac OpenStack OpenSSH Android BGL HDFS; do
  mkdir -p Logs/$ds; cp -L $R/Logs/$ds/$ds.log Logs/$ds/$ds.log
  ./bin/Delog_compress $ds text 100000 4 0 lzma normal >/dev/null 2>&1
  for run in 1 2 3; do
    s=$(date +%s%N); ./bin/dec_timed output/$ds r.log 4 >/dev/null 2>ph.txt; e=$(date +%s%N)
    cmp -s r.log Logs/$ds/$ds.log && ok=yes || ok=NO
    p=$(grep PHASES_US ph.txt | sed 's/.*extract=\([0-9]*\) load=\([0-9]*\) rebuild=\([0-9]*\)/\1,\2,\3/')
    echo "$ds,$run,$(( (e-s)/1000000 )),$p,$ok"; rm -rf r.log decompress_temp
  done
  rm -rf output/$ds Logs/$ds
done
cd /; rm -rf $L
