import argparse

def calculate_ratio(file_path):
    """
    Calculates the ratio of digit characters to other characters in a file.
    """
    digit_count = 0
    other_count = 0
    
    with open(file_path, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            for ch in line:
                if ch.isdigit():
                    digit_count += 1
                else:
                    other_count += 1
    
    total = digit_count + other_count
    if total == 0:
        return 0, 0, 0.0
    
    ratio = digit_count / total
    return digit_count, other_count, ratio


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Calculate the ratio of digit characters to other characters in a log file.")
    parser.add_argument("file", help="Path to the input log file.")
    args = parser.parse_args()
    
    digits, others, ratio = calculate_ratio(args.file)
    print(f"Number of digit characters: {digits}")
    print(f"Number of other characters: {others}")
    print(f"Ratio of digit characters: {ratio:.4f}")