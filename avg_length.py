#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Calculates the number of characters per line in a log file and outputs the average.
"""

def average_line_length(log_file_path):
    total_chars = 0
    total_lines = 0

    with open(log_file_path, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            total_chars += len(line.rstrip("\n"))  # Count characters without the newline
            total_lines += 1

    if total_lines == 0:
        return 0

    return total_chars / total_lines


if __name__ == "__main__":
    import sys
    if len(sys.argv) != 2:
        print(f"Usage: python {sys.argv[0]} <log_file>")
        sys.exit(1)

    log_file = sys.argv[1]
    avg_length = average_line_length(log_file)
    print(f"The average line length for the log file {log_file} is: {avg_length:.2f} characters")