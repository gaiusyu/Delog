# Revision experiments

Scripts used for the revised TSC manuscript. All results in the revised paper were produced with
the source and binaries in the repository root, on a Debian 11.9 container (Linux 5.4.143,
GCC 10.2.1, 8 CPU cores, 32 GB memory), with 100K-line chunks, 4 threads, and lzma unless noted.

1. Prepare a directory `$R` with
   - `src/`: `compressor.cpp`, `decompressor.cpp`, `BS_thread_pool.hpp`, `Denum.cpp`,
     `Denum_decompress.cpp` (from `Baselines/Denum`), and the generated variants below;
   - `Logs/<dataset>/<dataset>.log` for the 16 Loghub datasets.
2. Generate the variants:
   - `python3 make_variants.py ../compressor.cpp $R/src` writes the ablation Settings 1 and 2
     and the variable-anchor stress variant.
   - `python3 make_timed_decompressor.py ../decompressor.cpp $R/src/decompressor_timed.cpp`
     adds phase timers to the decompressor.
3. Copy `run_all_revision.sh` and `run_scalability.sh` to `$R` and run
   `./run_all_revision.sh` and then `./run_scalability.sh`. Data are copied to local disk
   (`DELOG_WORK`, default `/tmp/delog_r2`), and CSV results are written to `$R/results/`:
   - `r2_main_speed.csv`: CR, compression and decompression time, peak memory, restored size, and
     byte-for-byte comparison (`cmp`) of DeLog, DeLog-L, and Denum, measured in one session. It gives
     the DeLog and DeLog-L rows of Table V, the Denum CS row, and Tables S2-S3. The CR rows of
     LogReducer, LogShrink, and Denum in Table V come from the original runs. Denum needs a high
     open-file limit, which the script sets with `ulimit -n`.
   - `r2_decomp_phases.csv`: decompression phase timing of DeLog and DeLog-L (Table S6).
   - `r2_chunk_sensitivity.csv`: chunk sizes 10K-500K lines.
   - `r2_recognizer_off.csv`: DeLog without the dataset-specific recognizers.
   - `r2_ablation.csv`: ablation Settings 1 and 2 (Setting 3 is the main run).
   - `r2_anchor_stress.csv`: variable-anchor stress test (Table S7).
   - `r2_scalability.csv`: 1-8 threads for DeLog, Denum, and parallel lzma.
4. Summarize and plot: `python3 summarize_r2.py $R/results`,
   `python3 plot_ablation.py $R/results ablation.pdf`, and
   `python3 plot_scalability_cpu.py $R/results/r2_scalability.csv scalability.pdf cpu.pdf`.
