import argparse
import re

def calculate_token_ratio(file_path, max_lines=100000):
    # Regex for delimiters: space, comma, colon, equals sign, hyphen, slash
    splitter = re.compile(r"[ ,:=/-]+")
    # Regex to match tokens containing a digit
    has_digit = re.compile(r"\d")

    total_tokens = 0
    digit_tokens = 0

    with open(file_path, "r", encoding="utf-8", errors="ignore") as f:
        for i, line in enumerate(f):
            if i >= max_lines:
                break
            tokens = splitter.split(line.strip())
            tokens = [t for t in tokens if t]  # Remove empty tokens

            total_tokens += len(tokens)
            digit_tokens += sum(1 for t in tokens if has_digit.search(t))

    if total_tokens == 0:
        return 0, 0, 0.0

    ratio = digit_tokens / total_tokens
    return digit_tokens, total_tokens, ratio


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Calculate the ratio of tokens containing digits using regex (limited to a max of 100k lines).")
    parser.add_argument("file", help="Path to the input log file.")
    parser.add_argument("--max-lines", type=int, default=100000, help="Maximum number of log lines to process (default: 100000)")
    args = parser.parse_args()

    digit_tokens, total_tokens, ratio = calculate_token_ratio(args.file, args.max_lines)
    print(f"Total number of tokens: {total_tokens}")
    print(f"Number of tokens containing digits: {digit_tokens}")
    print(f"Ratio of tokens containing digits: {ratio:.4f}")