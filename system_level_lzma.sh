#!/bin/bash

# ==============================================================================
# Unified System-Level Performance Evaluation Script for Parallel LZMA (xz)
#
# This script adapts the standard evaluation framework to benchmark a parallel,
# block-based compression strategy using standard command-line tools (`split`,
# `find`, `xargs`, `xz`).
#
# It measures the total system-level performance of the entire pipeline
# (splitting + parallel compression) for various thread counts.
# ==============================================================================

# --- Configuration ---

# --- Test Scenarios ---
# The input file to be compressed.
INPUT_FILE="Logs/bytedance_C/bytedance_C.log"

# --- Scalability Test Configuration ---
# List of thread counts to test for parallel compression.
THREAD_COUNTS_TO_TEST=(1 2 4 8 16)

# --- Tool and Directory Configurations ---
# Get the absolute path of the script's directory for robust path handling.
SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &> /dev/null && pwd)

# Parameters for the benchmark
LINES_PER_BLOCK=100000
COMPRESSION_TOOL="xz -9" # -9 for highest compression.

# Directory where evaluation results (stats files and summary) will be saved.
EVALUATION_OUTPUT_DIR="${SCRIPT_DIR}/evaluation_results_lzma"
# Temporary directory for file chunks. It will be created and cleaned up.
TEMP_DIR_BASE="${EVALUATION_OUTPUT_DIR}/temp_chunks"


# ==============================================================================
# ==                            SCRIPT CORE LOGIC                             ==
# ==============================================================================

# --- Function Definitions ---

# Function: Executes a single, full test cycle for `time` to measure.
# This function encapsulates the entire split -> compress pipeline.
# $1: Unused (placeholder for dataset, could be adapted for multiple files)
# $2: Number of Threads
run_single_test_cycle() {
    local dataset_placeholder=$1
    local threads=$2
    
    # Each run gets its own isolated temporary directory
    local temp_dir="${TEMP_DIR_BASE}/${threads}_threads"
    mkdir -p "$temp_dir"

    # --- Step 1: Split the large file into chunks in the temp directory ---
    # We redirect internal echo to stderr so it doesn't show up in time's stdout capture
    # but still shows on console if you run the command directly for debugging.
    # >/dev/null suppresses stdout of the command itself.
    split -l ${LINES_PER_BLOCK} "${SCRIPT_DIR}/${INPUT_FILE}" "${temp_dir}/chunk_" > /dev/null

    # --- Step 2: Use `find` + `xargs` to compress chunks in parallel ---
    # `-P ${threads}` tells xargs how many parallel processes to use.
    # `-c {} > /dev/null` discards the compressed output to measure pure CPU performance.
    find "$temp_dir" -type f -name 'chunk_*' | xargs -P "$threads" -I {} ${COMPRESSION_TOOL} -c {} > /dev/null

    # --- Step 3: Cleanup temporary directory for this run ---
    # This is important for accurate I/O measurement across runs.
    rm -rf "$temp_dir"
}

# Function: Parses all result files and generates a CSV summary report.
# (This function is generic and does not need changes)
generate_summary_csv() {
    local summary_file="${EVALUATION_OUTPUT_DIR}/summary.csv"
    echo "Generating summary CSV file: ${summary_file}"

    echo "TestName,Dataset,Threads,WallClock_s,UserTime_s,SystemTime_s,CpuUsage_percent,PeakMemory_MB,FileSystem_Inputs,FileSystem_Outputs" > "$summary_file"

    for stats_file in "${EVALUATION_OUTPUT_DIR}"/*_stats.txt; do
        if [ ! -f "$stats_file" ]; then continue; fi
        local filename=$(basename "$stats_file" _stats.txt)
        local threads_part=$(echo "$filename" | grep -oE '[0-9]+threads$')
        local threads=${threads_part%threads}
        local dataset=${filename%_$threads_part}

        local metrics=$(awk '
            BEGIN { FS = ": "; OFS = ","; wall_s=0; user_s=0; sys_s=0; cpu_p=0; mem_mb=0; fs_in=0; fs_out=0; }
            /Elapsed <span data-type="inline-math" data-value="d2FsbCBjbG9jaw=="></span> time/ {
                n = split($2, a, ":");
                if (n == 3) wall_s = a[1]*3600 + a[2]*60 + a[3];
                else if (n == 2) wall_s = a[1]*60 + a[2];
                else wall_s = $2;
            }
            /User time <span data-type="inline-math" data-value="c2Vjb25kcw=="></span>/ { user_s = $2; }
            /System time <span data-type="inline-math" data-value="c2Vjb25kcw=="></span>/ { sys_s = $2; }
            /Percent of CPU this job got/ { cpu_p = $2; sub(/%/, "", cpu_p); }
            /Maximum resident set size <span data-type="inline-math" data-value="a2J5dGVz"></span>/ { mem_mb = $2 / 1024; }
            /File system inputs/ { fs_in = $2; }
            /File system outputs/ { fs_out = $2; }
            END {
                printf "%.2f,%.2f,%.2f,%s,%.2f,%d,%d", wall_s, user_s, sys_s, cpu_p, mem_mb, fs_in, fs_out
            }
        ' "$stats_file")

        echo "${filename},${dataset},${threads},${metrics}" >> "$summary_file"
    done

    echo "CSV summary generated successfully."
    echo "--------------------------------------------------------------------------------------------------"
    if [ -s "$summary_file" ] && [ "$(wc -l < "$summary_file")" -gt 1 ]; then
        column -s, -t < "$summary_file"
    else
        echo "No successful runs found to summarize."
    fi
    echo "--------------------------------------------------------------------------------------------------"
}

# --- Main Execution Logic ---

echo "======================================================"
echo "    Starting System-Level Evaluation for Parallel LZMA"
echo "======================================================"

# 1. Check dependencies
for tool in /usr/bin/time split find xargs xz; do
    if ! command -v "$tool" &> /dev/null; then
        echo "Error: Required command '$tool' not found. Please install it." >&2
        exit 1
    fi
done
if [ ! -f "${SCRIPT_DIR}/${INPUT_FILE}" ]; then
    echo "Error: Input file not found at '${SCRIPT_DIR}/${INPUT_FILE}'" >&2
    exit 1
fi

# 2. Setup directory
mkdir -p "$EVALUATION_OUTPUT_DIR"
# The temporary directory is now managed inside the test function

# 3. Export function and variables for the subshell
export -f run_single_test_cycle
export SCRIPT_DIR INPUT_FILE LINES_PER_BLOCK COMPRESSION_TOOL TEMP_DIR_BASE

# 4. Run all benchmark combinations
# We use a placeholder for the dataset loop since we only have one input file.
DATASETS_TO_TEST=("Parallel_LZMA")
total_tests=$(( ${#DATASETS_TO_TEST[@]} * ${#THREAD_COUNTS_TO_TEST[@]} ))
current_test=0

for dataset in "${DATASETS_TO_TEST[@]}"; do
    for threads in "${THREAD_COUNTS_TO_TEST[@]}"; do
        ((current_test++))
        test_name="${dataset}_${threads}threads"
        stats_file="${EVALUATION_OUTPUT_DIR}/${test_name}_stats.txt"

        echo -e "\n--- [START] Benchmarking: ${test_name} (${current_test}/${total_tests}) ---"

        # Execute the function in a new subshell measured by `time`
        /usr/bin/time -v -o "$stats_file" bash -c 'run_single_test_cycle "$@"' -- "$dataset" "$threads"

        if [ $? -eq 0 ]; then
            echo "Success. Statistics saved to: ${stats_file}"
        else
            echo "Error: Test failed for '${test_name}'. Check stats file for details."
        fi
        echo "--- [END] Benchmarking: ${test_name} ---"
    done
done

# 5. Generate and print the final summary report
generate_summary_csv

echo -e "\n======================================================"
echo "          Evaluation Complete for Parallel LZMA!"
echo "======================================================"