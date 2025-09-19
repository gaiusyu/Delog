import os
import sys
import time
import gzip
import brotli
import lz4.frame
import argparse
import itertools
from concurrent.futures import ProcessPoolExecutor, as_completed

# ===================================================================
# Helper Functions
# ===================================================================

def format_bytes(size):
    """Formats bytes into a more readable format (KB, MB, GB)."""
    if size == 0:
        return "0 B"
    power = 1024
    n = 0
    power_labels = {0: 'B', 1: 'KB', 2: 'MB', 3: 'GB', 4: 'TB'}
    while size >= power and n < len(power_labels) - 1:
        size /= power
        n += 1
    return f"{size:.2f} {power_labels[n]}"

def get_compressor(algorithm_name):
    """Returns a compression function and file extension based on the algorithm name."""
    compressors = {
        'gzip': (gzip.compress, '.gz'),
        'brotli': (brotli.compress, '.br'),
        'lz4': (lz4.frame.compress, '.lz4')
    }
    if algorithm_name.lower() not in compressors:
        raise ValueError(f"Unsupported algorithm: {algorithm_name}. Options are: gzip, brotli, lz4")
    return compressors[algorithm_name.lower()]

# ===================================================================
# Core Worker Function
# ===================================================================

def compress_chunk(chunk_data, chunk_id, num_lines, output_prefix, compress_func, extension):
    """
    Compresses a single data chunk (composed of multiple lines) and writes it to a file.
    """
    start_time = time.perf_counter()
    
    compressed_data = compress_func(chunk_data)
    output_path = f"{output_prefix}.chunk{chunk_id}{extension}"
    
    with open(output_path, 'wb') as f:
        f.write(compressed_data)
        
    duration = time.perf_counter() - start_time
    
    return {
        'chunk_id': chunk_id,
        'num_lines': num_lines,
        'original_size': len(chunk_data),
        'compressed_size': len(compressed_data),
        'duration': duration,
        'output_path': output_path
    }

# ===================================================================
# Main Flow Control
# ===================================================================

def stream_compress_parallel_by_lines(filepath, lines_per_chunk, num_threads, algorithm):
    """
    Main function that chunks by line count and compresses in parallel.
    """
    # 1. Validate parameters and prepare
    try:
        original_total_size = os.path.getsize(filepath)
        compress_func, extension = get_compressor(algorithm)
    except FileNotFoundError:
        print(f"Error: File not found '{filepath}'")
        sys.exit(1)
    except ValueError as e:
        print(f"Error: {e}")
        sys.exit(1)

    output_dir = f"{os.path.basename(filepath)}_compressed_chunks_{algorithm}"
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)
    output_prefix = os.path.join(output_dir, os.path.basename(filepath))

    print("=" * 60)
    print(f"Starting parallel compression for file: {os.path.basename(filepath)}")
    print(f"Original total size: {format_bytes(original_total_size)}")
    print(f"Chunking standard: {lines_per_chunk:,} lines/chunk")
    print(f"Using threads: {num_threads}")
    print(f"Compression algorithm: {algorithm.upper()}")
    print("=" * 60)
    
    total_compressed_size = 0
    total_chunks = 0
    total_lines = 0
    main_start_time = time.perf_counter()

    # 2. Use a process pool for parallel processing
    with ProcessPoolExecutor(max_workers=num_threads) as executor:
        futures = []
        chunk_id = 0
        
        # 3. Stream the file and submit tasks chunked by line count
        # Must open in binary mode ('rb') as compression functions require bytes
        with open(filepath, 'rb') as f:
            while True:
                # Use itertools.islice to efficiently read N lines from the file without loading the entire file into memory
                lines_buffer = list(itertools.islice(f, lines_per_chunk))
                
                if not lines_buffer:
                    break  # End of file reached

                # Join the list of lines (bytes list) into a single large bytes object
                chunk_data = b''.join(lines_buffer)
                num_lines_in_chunk = len(lines_buffer)
                
                # Submit the compression task to the process pool
                future = executor.submit(
                    compress_chunk, 
                    chunk_data, 
                    chunk_id, 
                    num_lines_in_chunk,
                    output_prefix,
                    compress_func,
                    extension
                )
                futures.append(future)
                chunk_id += 1

        # 4. Collect and report results
        print("All chunks have been dispatched, waiting for compression tasks to complete...")
        for future in as_completed(futures):
            try:
                result = future.result()
                total_chunks += 1
                total_lines += result['num_lines']
                total_compressed_size += result['compressed_size']
                
                ratio = result['original_size'] / result['compressed_size'] if result['compressed_size'] > 0 else float('inf')

                print(
                    f"  [Chunk {result['chunk_id']}] Complete. "
                    f"({result['num_lines']:,} lines, {format_bytes(result['original_size'])}) -> "
                    f"{format_bytes(result['compressed_size'])}, "
                    f"Ratio {ratio:.2f}:1, "
                    f"Time {result['duration']:.3f}s."
                )
            except Exception as e:
                print(f"A compression task failed: {e}")

    main_end_time = time.perf_counter()
    
    # 5. Final summary
    print("-" * 60)
    print("Compression Summary:")
    print(f"  Total chunks processed: {total_chunks}")
    print(f"  Total lines processed: {total_lines:,}")
    print(f"  Original total size: {format_bytes(original_total_size)}")
    print(f"  Total compressed size: {format_bytes(total_compressed_size)}")
    
    if total_compressed_size > 0:
        overall_ratio = original_total_size / total_compressed_size
        print(f"  Overall compression ratio: {overall_ratio:.2f} : 1")
    
    print(f"  Total time taken: {main_end_time - main_start_time:.2f} seconds")
    print(f"  Compressed chunks saved to directory: '{output_dir}'")
    print("-" * 60)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Stream and compress a large file in parallel by chunking based on a specified number of lines.",
        formatter_class=argparse.RawTextHelpFormatter
    )
    parser.add_argument("filepath", help="Path to the large file to be compressed.")
    parser.add_argument(
        "-l", "--lines-per-chunk", 
        type=int, 
        default=1000000, 
        help="Number of lines per data chunk. Default: 1,000,000"
    )
    parser.add_argument(
        "-t", "--threads", 
        type=int, 
        default=os.cpu_count(), 
        help=f"Number of parallel threads/processes to use for compression. Default: System CPU core count ({os.cpu_count()})"
    )
    parser.add_argument(
        "-a", "--algorithm", 
        type=str, 
        default='gzip', 
        choices=['gzip', 'brotli', 'lz4'],
        help="Compression algorithm to use. Options: gzip, brotli, lz4. Default: gzip"
    )

    args = parser.parse_args()
    stream_compress_parallel_by_lines(args.filepath, args.lines_per_chunk, args.threads, args.algorithm)