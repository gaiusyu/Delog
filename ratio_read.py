import os
import pandas as pd

# Configure paths
log_base_name = "bytedance"
log_file_path = f"Logs/{log_base_name}/{log_base_name}.log"

compressed_files = {
    "Brotli": f"{log_base_name}.log_compressed_chunks_brotli",
    "Gzip": f"{log_base_name}.log_compressed_chunks_gzip",
    "LZ4": f"{log_base_name}.log_compressed_chunks_lz4",
    "MyAlgorithm_LZMA": f"output/{log_base_name}",  # Your custom algorithm
}

def get_size(path):
    """Get the size of a file or directory in bytes."""
    if os.path.isfile(path):
        return os.path.getsize(path)
    elif os.path.isdir(path):
        total = 0
        for root, _, files in os.walk(path):
            for f in files:
                total += os.path.getsize(os.path.join(root, f))
        return total
    else:
        return 0

# Original file size
original_size = get_size(log_file_path)

# Collect results
results = []
for algo, path in compressed_files.items():
    comp_size = get_size(path)
    if comp_size > 0:
        ratio = comp_size / original_size
        savings = 1 - ratio
        results.append([algo, comp_size, f"{ratio:.2%}", f"{savings:.2%}"])
    else:
        results.append([algo, "N/A", "N/A", "N/A"])

# Print the table
df = pd.DataFrame(results, columns=["Algorithm", "Compressed Size (bytes)", "Compression Ratio", "Space Savings"])
print(df.to_string(index=False))