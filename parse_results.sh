#!/bin/bash

RESULTS_DIR="benchmark_results"

if [ ! -d "$RESULTS_DIR" ]; then
    echo "Error: Results directory '$RESULTS_DIR' not found. Please run ./benchmark.sh first."
    exit 1
fi

# Print the CSV header
printf "%-20s, %-15s, %-15s, %-20s, %-15s, %-15s, %-15s\n" \
    "Algorithm" "User Time(s)" "System Time(s)" "Total CPU Time(s)" "Elapsed Time" "Max Memory(KB)" "CPU Usage(%)"

# Iterate through all statistics files
for stats_file in ${RESULTS_DIR}/*_stats.txt; do
    if [ ! -f "$stats_file" ]; then continue; fi

    # Extract the algorithm name from the filename
    algorithm_name=$(basename "$stats_file" _stats.txt)

    # Use grep and awk to extract key data from the file
    user_time=$(grep 'User time' "$stats_file" | awk '{print $4}')
    sys_time=$(grep 'System time' "$stats_file" | awk '{print $4}')
    elapsed_time=$(grep 'Elapsed (wall clock) time' "$stats_file" | awk '{print $6}')
    max_mem_kb=$(grep 'Maximum resident set size' "$stats_file" | awk '{print $6}')
    cpu_percent=$(grep 'Percent of CPU this job got' "$stats_file" | awk '{print $6}' | sed 's/%//')
    
    # Use bc for floating-point calculation to get total CPU time
    total_cpu_time=$(echo "$user_time + $sys_time" | bc)

    # Format the output
    printf "%-20s, %-15.3f, %-15.3f, %-20.3f, %-15s, %-15s, %-15s\n" \
        "$algorithm_name" "$user_time" "$sys_time" "$total_cpu_time" "$elapsed_time" "$max_mem_kb" "$cpu_percent"
done | sort