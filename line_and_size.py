#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Counts the total number of lines and the file size (in GB) of a log file.
"""

import os

def count_lines(file_path):
    count = 0
    with open(file_path, "r", encoding="utf-8", errors="ignore") as f:
        for _ in f:
            count += 1
    return count

def file_size_gb(file_path):
    size_bytes = os.path.getsize(file_path)
    return size_bytes / (1024 ** 3)  # Convert to GB

if __name__ == "__main__":
    import sys
    if len(sys.argv) != 2:
        print(f"Usage: python {sys.argv[0]} <log_file>")
        sys.exit(1)

    log_file = sys.argv[1]
    total_lines = count_lines(log_file)
    size_gb = file_size_gb(log_file)

    print(f"Log file: {log_file}")
    print(f"  Total lines: {total_lines}")
    print(f"  File size: {size_gb:.3f} GB")