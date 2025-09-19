#!/usr/bin/env python3
import argparse
import os
import sys
import time

# The name of the file where comparison results will be logged.
LOG_FILENAME = "comparison_log.csv"

def log_comparison_result(dataset_name, result_status, diff_count, lines_checked):
    """
    Logs the result of a file comparison to a CSV file.

    :param dataset_name: The name of the dataset being compared.
    :param result_status: A string describing the result (e.g., 'Identical').
    :param diff_count: The number of differences found.
    :param lines_checked: The total number of lines compared.
    """
    # Check if the log file needs a header.
    write_header = not os.path.exists(LOG_FILENAME) or os.path.getsize(LOG_FILENAME) == 0

    try:
        with open(LOG_FILENAME, "a", encoding="utf-8") as log_file:
            if write_header:
                log_file.write("Timestamp,DatasetName,Result,DifferencesFound,LinesChecked\n")
            
            timestamp = time.strftime("%Y-%m-%d %H:%M:%S")
            log_entry = (
                f'{timestamp},{dataset_name},{result_status},'
                f'{diff_count},{lines_checked}\n'
            )
            log_file.write(log_entry)
        print(f"Result successfully logged to '{LOG_FILENAME}'")
    except Exception as e:
        print(f"\nError: Could not write to log file '{LOG_FILENAME}'.", file=sys.stderr)
        print(f"Details: {e}", file=sys.stderr)

def compare_files_line_by_line(file1, file2, dataset_name, max_diffs=10, encoding="utf-8"):
    """
    Compares two large files line by line in a streaming fashion, printing differences.
    
    :param file1: Path to the first file.
    :param file2: Path to the second file.
    :param dataset_name: The name of the dataset for logging purposes.
    :param max_diffs: The maximum number of differences to display.
    :param encoding: The file encoding to use.
    """
    print(f"--- Starting File Comparison ---")
    print(f"File 1: {file1}")
    print(f"File 2: {file2}")
    print(f"------------------------------")

    line_num = 0
    diff_count = 0
    files_are_identical = True

    try:
        with open(file1, "r", encoding=encoding, errors="ignore") as f1, \
             open(file2, "r", encoding=encoding, errors="ignore") as f2:

            while True:
                line1 = f1.readline()
                line2 = f2.readline()
                line_num += 1

                # If both files have reached their end, break the loop.
                if not line1 and not line2:
                    break
                
                # --- Core Comparison Logic ---
                # Compare content after stripping trailing newlines. This ignores differences
                # caused by a final empty line in one file. Using rstrip('\n') instead of
                # strip() preserves meaningful leading/trailing whitespace.
                if line1.rstrip('\n') != line2.rstrip('\n'):
                    files_are_identical = False
                    diff_count += 1
                    print(f"\n>> Difference #{diff_count} (Line {line_num}):")
                    # Use strip() for cleaner display of the differing lines.
                    print(f"File 1 > {line1.strip()}")
                    print(f"File 2 > {line2.strip()}")

                    if diff_count >= max_diffs:
                        print(f"\nReached maximum difference limit of {max_diffs}. Stopping early.")
                        break
            
            # --- Final Summary ---
            if files_are_identical:
                print("\nSUCCESS: Lossless compression achieved! Files are identical.")
                result_status = "Identical"
            else:
                print(f"\nComparison finished. Found {diff_count} difference(s).")
                result_status = "Differences Found"

            # Log the final result to the CSV file.
            # (line_num - 1) gives the total lines processed.
            log_comparison_result(dataset_name, result_status, diff_count, line_num - 1)

    except FileNotFoundError as e:
        print(f"\nError: File not found. Please check the file paths.", file=sys.stderr)
        print(f"Details: {e}", file=sys.stderr)
        sys.exit(1)
    except Exception as e:
        print(f"\nAn unknown error occurred: {e}", file=sys.stderr)
        sys.exit(1)

def main():
    """Main function to parse command-line arguments and invoke the core functionality."""
    parser = argparse.ArgumentParser(
        description="A command-line tool to compare two log files line by line.",
        epilog="Example: python %(prog)s OpenSSH -m 5"
    )

    parser.add_argument(
        "log_name",
        type=str,
        help="The name of the log to compare, e.g., 'OpenSSH'."
    )
    
    parser.add_argument(
        "-m", "--max-diffs",
        type=int,
        default=10,
        help="The maximum number of differences to display (default: 10)."
    )
    parser.add_argument(
        "-e", "--encoding",
        type=str,
        default="utf-8",
        help="The file encoding to use (default: utf-8)."
    )

    args = parser.parse_args()

    name = args.log_name
    # File paths are derived from the log name, as per your requirement.
    file1 = f"restored_logs/decompressed_{name}.log"
    file2 = f"Logs/{name}/{name}.log"

    # Verify that both files exist before starting.
    if not os.path.exists(file1):
        print(f"Error: File '{file1}' does not exist.", file=sys.stderr)
        sys.exit(1)
    if not os.path.exists(file2):
        print(f"Error: File '{file2}' does not exist.", file=sys.stderr)
        sys.exit(1)

    compare_files_line_by_line(
        file1,
        file2,
        dataset_name=name,
        max_diffs=args.max_diffs,
        encoding=args.encoding
    )

if __name__ == "__main__":
    main()