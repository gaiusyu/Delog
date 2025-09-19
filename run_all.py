import os
import subprocess

# Define all dataset names
datasets = [
    "Android",
    "Apache",
    "BGL",
    "Hadoop",
    "HDFS",
    "HealthApp",
    "HPC",
    "Linux",
    "Mac",
    "OpenSSH",
    "OpenStack",
    "Proxifier",
    "Spark",
    "Thunderbird",
    "Windows",
    "Zookeeper"
]

# Define command-line arguments
origin_file_path = "Loghub_data/"
input_dir = "Drain_result/"

# Iterate through all datasets and execute P_C.py
for dataset in datasets:
    print(f"Processing dataset: {dataset}")
    command = [
        "python", "P_C.py",
        "--origin_file_path", origin_file_path,
        "--input_dir", input_dir,
        "--logname", dataset
    ]
    
    # Execute the command
    subprocess.run(command)
    
    print(f"Finished processing dataset: {dataset}\n")

print("All datasets processed successfully!")