#!/bin/bash

# ==============================================================================
# Ultimate All-in-One System Performance Evaluation Script
#
# Features:
# 1. Automatically iterates through different datasets and thread counts to test scalability.
# 2. For each test combination, it internally executes a full cycle: Compression -> Decompression -> Cleanup.
# 3. Uses GNU `time` to record detailed performance metrics, including:
#    - Wall Clock Time
#    - CPU Time (User + System)
#    - Peak Memory Usage
#    - Filesystem I/O (Filesystem Inputs/Outputs)
# 4. Aggregates all results into a single CSV file for easy analysis and visualization.
#
# Usage:
# 1. Place this script in the same directory as the C++ executables (compressor, decompressor).
# 2. Grant execution permission: `chmod +x system_evaluation.sh`
# 3. Modify the test parameters in the configuration section below.
# 4. Run the script: `./system_evaluation.sh`
# ==============================================================================

# --- Configuration ---
# (*** Configure your test scenarios here ***)

# List of datasets to test
DATASETS_TO_TEST=(
    "bytedance_C"
    # "HDFS"
    # "BGL"
)

# List of thread counts to test (for scalability analysis)
THREAD_COUNTS_TO_TEST=(1 2 4 8 16)

# --- Core Variable Definitions ---

# Get the directory where the script is located to ensure correct paths
SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &> /dev/null && pwd)

# Define executables with absolute paths to prevent issues in sub-shells
COMPRESSOR_EXE="${SCRIPT_DIR}/compressor"
DECOMPRESSOR_EXE="${SCRIPT_DIR}/decompressor"

# Other fixed parameters
BLOCK_SIZE=100000
COMPRESS_MODE=1

# Result and temporary file directories
OUTPUT_DIR="evaluation_results"
RESULTS_BASE_DIR="Baseline/Denum/results" # Temporary directory for compressed files

# --- Function Definitions ---

# Function: Executes a single, complete test cycle (compress -> decompress -> cleanup)
# This function will be measured by the `time` command in a sub-shell
# $1: Dataset
# $2: Thread count
run_single_test() {
    local dataset=$1
    local threads=$2

    # Check if executables exist
    if [ ! -f "${COMPRESSOR_EXE}" ] || [ ! -f "${DECOMPRESSOR_EXE}" ]; then
        echo "!!! ERROR: One or more executables not found. Paths: ${COMPRESSOR_EXE}, ${DECOMPRESSOR_EXE}" >&2
        exit 1
    fi

    local compressed_dir="${RESULTS_BASE_DIR}/${dataset}"
    local restored_log="${dataset}_restored.log"

    # To avoid output interfering with `time`'s measurement, internal echos can be redirected to /dev/null
    local silent=true # Set to false to see detailed compression/decompression steps

    # 1. Run compression
    if $silent; then
        "${COMPRESSOR_EXE}" "$dataset" "$BLOCK_SIZE" "$COMPRESS_MODE" "$threads" > /dev/null
    else
        echo "  [Internal] Compressing..."
        "${COMPRESSOR_EXE}" "$dataset" "$BLOCK_SIZE" "$COMPRESS_MODE" "$threads"
    fi

    # 2. Run decompression
    if $silent; then
        "${DECOMPRESSOR_EXE}" "$compressed_dir" "$restored_log" "$threads" > /dev/null
    else
        echo "  [Internal] Decompressing..."
        "${DECOMPRESSOR_EXE}" "$compressed_dir" "$restored_log" "$threads"
    fi

    # 3. Clean up all intermediate files
    rm -rf "$compressed_dir"
    rm -f "$restored_log"
    if [ -d "denum_decompress_temp" ]; then
        rm -rf "denum_decompress_temp"
    fi
}

# Function: Parses all result files and generates a single CSV summary report
generate_summary_csv() {
    local summary_file="${OUTPUT_DIR}/summary.csv"
    echo "Generating summary CSV file: ${summary_file}"

    # CSV Header
    echo "TestName,Dataset,Threads,WallClock_s,UserTime_s,SystemTime_s,CpuUsage_percent,PeakMemory_MB,FileSystem_Inputs,FileSystem_Outputs" > "$summary_file"

    for stats_file in "${OUTPUT_DIR}"/*_stats.txt; do
        if [ ! -f "$stats_file" ]; then continue; fi

        local filename=$(basename "$stats_file" _stats.txt)
        local dataset=$(echo "$filename" | cut -d'_' -f2)
        local threads=$(echo "$filename" | cut -d'_' -f3)

        # Use AWK to efficiently extract all metrics at once
        local metrics=$(awk '
            BEGIN { FS = ": "; OFS = ","; wall_s=0; user_s=0; sys_s=0; cpu_p=0; mem_mb=0; fs_in=0; fs_out=0; }
            /Elapsed (wall clock) time/ {
                n = split($2, a, ":");
                if (n == 3) wall_s = a[1]*3600 + a[2]*60 + a[3];
                else if (n == 2) wall_s = a[1]*60 + a[2];
                else wall_s = $2;
            }
            /User time (seconds)/ { user_s = $2; }
            /System time (seconds)/ { sys_s = $2; }
            /Percent of CPU this job got/ { cpu_p = $2; sub(/%/, "", cpu_p); }
            /Maximum resident set size (kbytes)/ { mem_mb = $2 / 1024; }
            /File system inputs/ { fs_in = $2; }
            /File system outputs/ { fs_out = $2; }
            END {
                printf "%.2f,%.2f,%.2f,%s,%.2f,%d,%d", wall_s, user_s, sys_s, cpu_p, mem_mb, fs_in, fs_out
            }
        ' "$stats_file")

        echo "${filename},${dataset},${threads},${metrics}" >> "$summary_file"
    done

    echo "CSV summary generated successfully."
    echo "You can now import '${summary_file}' into a spreadsheet or plotting tool."
    echo "---------------------------------------------------------------------"
    if [ -s "$summary_file" ] && [ "$(wc -l < "$summary_file")" -gt 1 ]; then
        column -s, -t < "$summary_file"
    else
        echo "No successful runs found to summarize."
    fi
    echo "---------------------------------------------------------------------"
}

# --- Main Execution Logic ---

echo "======================================================"
echo "      Starting All-in-One Performance Evaluation"
echo "======================================================"
echo "Results will be stored in: ${OUTPUT_DIR}"
echo ""

# 1. Check dependencies
if ! command -v /usr/bin/time &> /dev/null; then
    echo "Error: GNU 'time' command not found at /usr/bin/time." >&2
    echo "Please install it. On Debian/Ubuntu: sudo apt-get install time" >&2
    exit 1
fi
if [ ! -f "${COMPRESSOR_EXE}" ] || [ ! -f "${DECOMPRESSOR_EXE}" ]; then
    echo "Error: '${COMPRESSOR_EXE}' or '${DECOMPRESSOR_EXE}' not found." >&2
    echo "Please ensure they are compiled and in the script's directory: ${SCRIPT_DIR}" >&2
    exit 1
fi

# 2. Create output directories
mkdir -p "$OUTPUT_DIR"
mkdir -p "$RESULTS_BASE_DIR"

# 3. Run all benchmarks
# `export -f` ensures the function is available in sub-shells
export -f run_single_test
# Also export variables needed by the sub-shell function
export COMPRESSOR_EXE DECOMPRESSOR_EXE BLOCK_SIZE COMPRESS_MODE RESULTS_BASE_DIR

for dataset in "${DATASETS_TO_TEST[@]}"; do
    for threads in "${THREAD_COUNTS_TO_TEST[@]}"; do
        test_name="Denum_${dataset}_${threads}_threads"
        stats_file="${OUTPUT_DIR}/${test_name}_stats.txt"

        echo "--- [START] Benchmarking: ${test_name} ---"

        # This is the key: execute the function in a new sub-shell and measure it with `time`
        # `bash -c '...'` starts a new shell
        # `"$@"` passes arguments safely to this new shell
        /usr/bin/time -v -o "$stats_file" bash -c 'run_single_test "$@"' -- "$dataset" "$threads"

        if [ $? -eq 0 ]; then
            echo "Success. Statistics saved to: ${stats_file}"
        else
            echo "Error: Test failed for '${test_name}'. Check statistics file for details (e.g., out-of-memory)."
            # Even on failure, the time command will still generate a stats file with partial information
        fi
        echo "--- [END] Benchmarking: ${test_name} ---"
        echo ""
    done
done

# 4. Generate and print the final summary report
generate_summary_csv

echo "======================================================"
echo "             Evaluation Complete!"
echo "======================================================"