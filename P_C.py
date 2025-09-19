import os
import argparse
import pandas as pd
import time
import csv
from Compressor import compression_tools

# Parse command-line arguments
parser = argparse.ArgumentParser(description="Process log data.")
parser.add_argument("--origin_file_path", type=str, default="Loghub_data/", help="Path to the origin log data directory.")
parser.add_argument("--input_dir", type=str, default="Drain_result/", help="Path to the input directory of log files.")
parser.add_argument("--logname", type=str, default="Apache", help="Name of the log file to process.")
args = parser.parse_args()

# Use command-line arguments
origin_file_path = args.origin_file_path
input_dir = args.input_dir
logname = args.logname

indir = os.path.join(input_dir, logname)
headh_dir = os.path.join("Drain_result/", logname)
groundtruth = os.path.join(indir, f"{logname}_full.log_structured.csv")
header_path = os.path.join(headh_dir, f"{logname}_full.log_structured.csv")

# Read the CSV file
start_time = time.perf_counter()
df_groundtruth = pd.read_csv(groundtruth, encoding="ISO-8859-1", header=0)
header_groundtruth = pd.read_csv(header_path, encoding="ISO-8859-1", header=0)

# Clear the output directory
compression_tools.clear_directory("Output")

# STEP 1: Store log event sequences
Event_sequences = df_groundtruth["EventTemplate"]

# STEP 2: Store Variables
Logs = df_groundtruth["Content"]

# Execute the extraction task
result, mapping, template_id, event_list = compression_tools.extract_variables_pure(Logs, Event_sequences)
compression_tools.store_content_with_ids(event_list, "Events")

for key in result.keys():
    path = os.path.join("Output", key)
    variable_list = result[key]
    num_tag = all(word.isdigit() for word in variable_list)
    
    # Process numbers differently from other variables
    if num_tag:
        # save_tmp = compression_tools.delta_transform(variable_list)
        # filename = path + ".bin"
        # with open(filename, 'ab') as file:
        #     for word in save_tmp:
        #         file.write(compression_tools.elastic_encoder(int(word)))
        save_tmp = variable_list
        compression_tools.store_content_with_ids(variable_list, key)
    else:
        compression_tools.store_content_with_ids(variable_list, key)

# STEP 3: Store Log Headers
for key in header_groundtruth.keys():
    if key not in ["EventId", "Content", "EventTemplate", "LineId", "ParameterList"]:
        path = os.path.join("Output", key)
        variable_list = header_groundtruth[key]
        num_tag = all(str(word).isdigit() for word in variable_list)
        
        # Process numbers differently from other variables
        if num_tag:
            save_tmp = compression_tools.delta_transform(variable_list)
            filename = path + ".bin"
            with open(filename, 'ab') as file:
                for word in save_tmp:
                    file.write(compression_tools.elastic_encoder(int(word)))
        else:
            compression_tools.store_content_with_ids(variable_list, key)

# STEP 4: General-purpose compression
compression_tools.compress_directory_to_lzma("Output", f"{logname}.lzma")

# Calculate file sizes and compression ratio
size = compression_tools.get_file_size(f"{logname}.lzma")
origin_size = compression_tools.get_file_size(os.path.join(origin_file_path, logname, f"{logname}_full.log"))
compression_ratio = origin_size / size if size > 0 else 0

# Calculate execution time
end_time = time.perf_counter()
execution_time_ms = (end_time - start_time) * 1000

# Save the results to a file
results_file = "experiment_results.csv"
file_exists = os.path.isfile(results_file)

with open(results_file, mode="a", newline="") as file:
    writer = csv.writer(file)
    # If the file does not exist, write the header row
    if not file_exists:
        writer.writerow(["Logname", "Original Size", "Achieved Size", "Compression Ratio", "Execution Time (ms)"])
    # Write the results of the current experiment
    writer.writerow([logname, origin_size, size, compression_ratio, execution_time_ms])

# Print the results
print(f"Logname: {logname}")
print(f"Original size: {origin_size}")
print(f"Achieved size: {size}")
print(f"Compression ratio: {compression_ratio}")
print(f"Execution time: {execution_time_ms:.2f} ms")