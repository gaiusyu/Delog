"""Add phase timers to decompressor.cpp for the decompression-bottleneck measurement.

Usage: python3 make_timed_decompressor.py ../decompressor.cpp OUT.cpp

The timed build prints one line to stderr at the end:
PHASES_US extract=<us> load=<us> rebuild=<us>
Each value is summed over all chunks and threads. extract covers reading the chunk archive and
lzma decoding, load covers building the stream providers (including elastic/delta decoding), and
rebuild covers token-by-token reconstruction.
"""
import sys


def sub_once(text, old, new):
    if text.count(old) != 1:
        raise SystemExit(f"pattern found {text.count(old)} times: {old[:80]!r}")
    return text.replace(old, new)


src = open(sys.argv[1]).read()
src = sub_once(src, "#include <iostream>", "#include <iostream>\n#include <atomic>\n#include <chrono>\n"
               "static std::atomic<long long> g_t_extract{0}, g_t_load{0}, g_t_rebuild{0};\n"
               "static inline long long now_us() { return std::chrono::duration_cast<std::chrono::microseconds>("
               "std::chrono::steady_clock::now().time_since_epoch()).count(); }")
src = sub_once(src, """    try {
        // 1. Extract the main archive file into the temporary directory.
        extract_archive(archive_path, temp_dir_path_);""", """    long long t_extract0 = now_us();
    try {
        // 1. Extract the main archive file into the temporary directory.
        extract_archive(archive_path, temp_dir_path_);
        g_t_extract += now_us() - t_extract0;""")
src = sub_once(src, """        Decompressor decompressor(chunk_path, temp_base_path);
        decompressor.decompress_to_stream(memory_stream);""", """        long long t0 = now_us();
        Decompressor decompressor(chunk_path, temp_base_path);
        long long t1 = now_us();
        decompressor.decompress_to_stream(memory_stream);
        g_t_rebuild += now_us() - t1;
        g_t_load += t1 - t0;""")
idx = src.rfind("    std::filesystem::remove_all(temp_base_path);")
src = (src[:idx] + "    std::cerr << \"PHASES_US extract=\" << g_t_extract.load() << \" load=\" << (g_t_load.load() - g_t_extract.load())"
       " << \" rebuild=\" << g_t_rebuild.load() << std::endl;\n" + src[idx:])
open(sys.argv[2], "w").write(src)
print("written", sys.argv[2])
