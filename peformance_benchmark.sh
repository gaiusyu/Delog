#!/bin/bash

# ==============================================================================
# System-Level Performance Evaluation Script for Delog
#
# This script is designed to replicate the workflow of the Python evaluation
# script, but using Bash and GNU `time` for detailed system-level metrics.
#
# It measures and compares performance (CPU, Memory, I/O) by running a
# compress -> decompress -> verify -> cleanup cycle for various datasets
# and thread counts.
# ==============================================================================

# --- Configuration (Aligns with the Python script) ---

# --- Test Scenarios ---
# List of datasets to test.
DATASETS_TO_TEST=(
    "bytedance_C"
    # "HDFS"
    # "BGL"
)

# List of thread counts for scalability testing.
THREAD_COUNTS_TO_TEST=(1 2 4 8 16)

# --- Executables and Paths ---
# Get the absolute path of the script's directory for robust path handling.
SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &> /dev/null && pwd)

# Paths to the executables.
COMPRESSOR_EXE="${SCRIPT_DIR}/Delog_compress"
DECOMPRESSOR_EXE="${SCRIPT_DIR}/decompress"
VERIFIER_SCRIPT="${SCRIPT_DIR}/diff_compare.py"

# --- Directory Configurations ---
LOG_INPUT_DIR="${SCRIPT_DIR}/Logs"
COMPRESSED_OUTPUT_DIR="${SCRIPT_DIR}/output"         # This is where the compressor writes
DECOMPRESSED_OUTPUT_DIR="${SCRIPT_DIR}/restored_logs" # This is where the decompressor writes
EVALUATION_OUTPUT_DIR="${SCRIPT_DIR}/evaluation_results" # For benchmark stats and summary

# --- Shared Pipeline Parameters ---
DATA_TYPE="text"
BATCH_SIZE=100000
COMPRESS_MODE="lzma"
LOG_TYPE="normal"
THRESHOLD=0 # Fixed threshold for all datasets.

# ==============================================================================
# ==                            SCRIPT CORE LOGIC                             ==
# ==============================================================================

# --- Function Definitions ---

# Function: Executes a single, full test cycle for `time` to measure.
# $1: Dataset Name
# $2: Number of Threads
run_single_test_cycle() {
    local dataset=$1
    local threads=$2

    # Define paths specific to this run
    local original_log_path="${LOG_INPUT_DIR}/${dataset}/${dataset}.log"
    local compressed_data_dir="${COMPRESSED_OUTPUT_DIR}/${dataset}"
    local decompressed_log_file="${DECOMPRESSED_OUTPUT_DIR}/decompressed_${dataset}.log"

    # --- Step 1: Compression ---
    "${COMPRESSOR_EXE}" \
        "$dataset" \
        "$DATA_TYPE" \
        "$BATCH_SIZE" \
        "$threads" \
        "$THRESHOLD" \
        "$COMPRESS_MODE" \
        "$LOG_TYPE" > /dev/null

    # --- Step 2: Decompression ---
    "${DECOMPRESSOR_EXE}" \
        "$compressed_data_dir" \
        "$decompressed_log_file" \
        "$threads" > /dev/null

    # --- Step 3: Verification (Optional but good practice) ---
    # Redirecting output as we only care if it runs for the benchmark.
    if [ -f "$VERIFIER_SCRIPT" ]; then
        python3 "$VERIFIER_SCRIPT" "$dataset" -m 1 > /dev/null
    fi

    # --- Step 4: Cleanup ---
    # This ensures each run is independent.
    if [ -d "$compressed_data_dir" ]; then
        rm -rf "$compressed_data_dir"
    fi
    if [ -f "$decompressed_log_file" ]; then
        rm -f "$decompressed_log_file"
    fi
}

# Function: Parses all result files and generates a CSV summary report.
# (This function is robust and does not need changes)
generate_summary_csv() {
    local summary_file="${EVALUATION_OUTPUT_DIR}/summary.csv"
    echo "Generating summary CSV file: ${summary_file}"

    echo "TestName,Dataset,Threads,WallClock_s,UserTime_s,SystemTime_s,CpuUsage_percent,PeakMemory_MB,FileSystem_Inputs,FileSystem_Outputs" > "$summary_file"

    for stats_file in "${EVALUATION_OUTPUT_DIR}"/*_stats.txt; do
        if [ ! -f "$stats_file" ]; then continue; fi

        local filename=$(basename "$stats_file" _stats.txt)
        local dataset=$(echo "$filename" | cut -d'_' -f1) # Adjusted for new TestName format
        local threads=$(echo "$filename" | cut -d'_' -f2 | sed 's/threads//') # Adjusted

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
echo "    Starting System-Level Evaluation for Delog"
echo "======================================================"

# 1. Check dependencies
if ! command -v /usr/bin/time &> /dev/null; then
    echo "Error: GNU 'time' command not found at /usr/bin/time. Please install it." >&2
    exit 1
fi
for exe in "$COMPRESSOR_EXE" "$DECOMPRESSOR_EXE"; do
    if [ ! -f "$exe" ]; then
        echo "Error: Executable not found: $exe" >&2
        exit 1
    fi
done

# 2. Setup directories
mkdir -p "$EVALUATION_OUTPUT_DIR"
mkdir -p "$COMPRESSED_OUTPUT_DIR"
mkdir -p "$DECOMPRESSED_OUTPUT_DIR"

# 3. Export function and variables to be available in the subshell
export -f run_single_test_cycle
export COMPRESSOR_EXE DECOMPRESSOR_EXE VERIFIER_SCRIPT LOG_INPUT_DIR \
       COMPRESSED_OUTPUT_DIR DECOMPRESSED_OUTPUT_DIR \
       DATA_TYPE BATCH_SIZE COMPRESS_MODE LOG_TYPE THRESHOLD

# 4. Run all benchmark combinations
total_tests=$(( ${#DATASETS_TO_TEST[@]} * ${#THREAD_COUNTS_TO_TEST[@]} ))
current_test=0

for dataset in "${DATASETS_TO_TEST[@]}"; do
    for threads in "${THREAD_COUNTS_TO_TEST[@]}"; do
        ((current_test++))
        test_name="${dataset}_${threads}threads"
        stats_file="${EVALUATION_OUTPUT_DIR}/${test_name}_stats.txt"

        echo -e "\n--- [START] Benchmarking: ${test_name} (${current_test}/${total_tests}) ---"

        # This is the key: Execute the function in a new subshell measured by `time`
        /usr/bin/time -v -o "$stats_file" bash -c 'run_single_test_cycle "$@"' -- "$dataset" "$threads"

        if [ $? -eq 0 ]; then
            echo "Success. Statistics saved to: ${stats_file}"
        else
            echo "Error: Test failed for '${test_name}'. Check stats file for details (e.g., out-of-memory)."
        fi
        echo "--- [END] Benchmarking: ${test_name} ---"
    done
done

# 5. Generate and print the final summary report
generate_summary_csv

echo -e "\n======================================================"
echo "          Evaluation Complete for Delog!"
echo "======================================================"