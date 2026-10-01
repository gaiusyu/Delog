# Revision experiments

Scripts used for the revised TSC manuscript. Both expect the layout
`./bin/Delog_compress`, `./bin/Delog_decompress`, and `./Logs/<dataset>/<dataset>.log`
in the directory that contains the script, and write CSV files to `./results/`.

- `run_main_table.sh <datasets...>`: DeLog (`normal`) and DeLog-L (`fast`) with 100K-line chunks,
  4 threads, and lzma; reports CR, compression/decompression speed, peak RSS, and the SHA-256
  round-trip result.
- `run_chunk_memory_decompression.sh "<datasets>" "<chunk sizes>" <threads>`: chunk-size
  sensitivity (CR and SHA-256 for each chunk size), signatures per chunk, peak RSS, and the
  time spent only in lzma/tar extraction versus full decompression.

Environment used in the paper: Debian 11.9 container, Linux 5.4.143, GCC 10.2.1, 4 threads.
