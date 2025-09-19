#!/bin/bash

# ==============================================================================
# Performance Benchmark Script
#
# This script measures and compares the performance (CPU time, wall clock time,
# and peak memory usage) of two Python scripts:
#   1. benchmark_logreducer.py
#   2. benchmark_bytelog.py
#
# It uses the GNU `time` command for accurate measurements and saves detailed
# logs for each run. Finally, it prints a summary table for easy comparison.
# ==============================================================================

# --- Configuration ---
# Directory where performance statistics will be saved
OUTPUT_DIR="benchmark_results"

# Commands to be benchmarked.
# If your scripts require arguments, add them here. For example:
# CMD_LOGREDUCER="python3 benchmark_logreducer.py --input some_file.log"
CMD_LOGREDUCER="python3 benchmark_logreducer.py"
# CMD_BYTELOG="python3 benchmark_bytelog.py"


# --- Script Core ---

# Function to run a benchmark for a given command
# $1: The name of the test (e.g., "LogReducer")
# $2...: The command and its arguments to execute
run_test() {
    local test_name=$1
    shift
    local cmd_to_run=("$@")
    local stats_file="${OUTPUT_DIR}/${test_name}_stats.txt"

    echo "--- [START] Benchmarking: ${test_name} ---"

    # Use /usr/bin/time with verbose output (-v)
    # -o: Write statistics to the specified file instead of stderr
    # > /dev/null: Redirect the command's standard output to null,
    #              so we only measure computational performance, not print/write speed.
    /usr/bin/time -v -o "$stats_file" "${cmd_to_run[@]}" > /dev/null

    if [ $? -eq 0 ]; then
        echo "Success. Statistics saved to: ${stats_file}"
    else
        echo "Error: Command failed for '${test_name}'. Check its output or the script itself."
        # We still have the stats file, which might contain info on why it failed (e.g., memory limit)
        echo "Partial statistics may be available in ${stats_file}"
    fi

    echo "--- [END] Benchmarking: ${test_name} ---"
    echo ""
}

# Function to parse result files and print a summary table
summarize_results() {
    echo "=========================================================================="
    echo "                         Benchmark Summary"
    echo "=========================================================================="
    
    local logreducer_stats="${OUTPUT_DIR}/LogReducer_stats.txt"
    local bytelog_stats="${OUTPUT_DIR}/ByteLog_stats.txt"

    if [[ ! -f "$logreducer_stats" || ! -f "$bytelog_stats" ]]; then
        echo "One or both statistics files are missing. Cannot generate summary."
        return
    fi

    # Header for the summary table
    printf "%-35s | %-20s | %-20s\n" "Metric" "LogReducer" "ByteLog"
    printf "--------------------------------------------------------------------------\n"

    # Define the metrics we want to extract and compare
    # The string is the exact pattern to find in the `time -v` output
    declare -a metrics_to_compare=(
        "Elapsed (wall clock) time"
        "User time (seconds)"
        "System time (seconds)"
        "Percent of CPU this job got"
        "Maximum resident set size (kbytes)"
    )

    for metric in "${metrics_to_compare[@]}"; do
        # Grep for the metric, use awk to grab the value after the colon
        local stat_logreducer=$(grep "$metric" "$logreducer_stats" | awk -F': ' '{print $2}')
        local stat_bytelog=$(grep "$metric" "$bytelog_stats" | awk -F': ' '{print $2}')
        
        # Format the metric name for printing
        local metric_name=$(echo "$metric" | sed 's/ (.*)//') # Remove units for display
        
        printf "%-35s | %-20s | %-20s\n" "$metric_name" "$stat_logreducer" "$stat_bytelog"
    done
    
    echo "=========================================================================="
}


# --- Main Execution ---

echo "Starting performance benchmark..."
echo "Results will be stored in the '${OUTPUT_DIR}' directory."
echo ""

# 1. Check if GNU time is available
if ! command -v /usr/bin/time &> /dev/null; then
    echo "Error: GNU 'time' command not found at /usr/bin/time." >&2
    echo "Please install it. On Debian/Ubuntu: sudo apt-get install time" >&2
    exit 1
fi

# 2. Create the output directory
mkdir -p "$OUTPUT_DIR"

# 3. Run the benchmarks
run_test "LogReducer" $CMD_LOGREDUCER
# run_test "ByteLog" $CMD_BYTELOG

# 4. Print the final summary
summarize_results

echo "Benchmark complete."