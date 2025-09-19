#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <map>
#include <filesystem> // C++17 for path manipulation
#include <regex>      // For simple replacements

// Must be defined before #include <pcre2.h>
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

namespace fs = std::filesystem;

// Helper function: Escape delimiter text for generating the final regex
std::string escape_regex_delimiter(const std::string& text) {
    std::string escaped;
    escaped.reserve(text.length());
    // PCRE2 special character set: . \ + * ? [ ^ ] $ ( ) { } = ! < > | : - #
    const std::string special_chars = ".\\+*?[]^$(){}=!<>|:-#";
    for (char c : text) {
        if (special_chars.find(c) != std::string::npos) {
            escaped += '\\';
        }
        escaped += c;
    }
    return escaped;
}

// Core function: Generate a list of headers and a PCRE2 regex from a log_format string
std::pair<std::vector<std::string>, std::string> generate_logformat_regex(const std::string& logformat) {
    std::vector<std::string> headers;
    std::string regex_pattern;
    size_t last_pos = 0;

    std::regex header_tag_regex(R"(<([^<>]+)>)");
    auto words_begin = std::sregex_iterator(logformat.begin(), logformat.end(), header_tag_regex);
    auto words_end = std::sregex_iterator();

    for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
        std::smatch match = *i;
        // 1. Add the delimiter before the <Header>
        if (match.position() > last_pos) {
            std::string delimiter = logformat.substr(last_pos, match.position() - last_pos);
            // Replace multiple consecutive spaces with \s+ (one or more whitespace characters)
            delimiter = std::regex_replace(delimiter, std::regex(" +"), R"(\s+)");
            regex_pattern += escape_regex_delimiter(delimiter);
        }

        // 2. Add the Header and a named capture group
        std::string header_name = match[1].str();
        headers.push_back(header_name);
        // Use PCRE2 named capture group (?<name>...?), where .*? is a non-greedy match
        regex_pattern += "(?<" + header_name + ">.*?)";
        
        last_pos = match.position() + match.length();
    }

    // Add the content after the last <Header>
    if (last_pos < logformat.length()) {
        std::string tail = logformat.substr(last_pos);
        tail = std::regex_replace(tail, std::regex(" +"), R"(\s+)");
        regex_pattern += escape_regex_delimiter(tail);
    }
    
    // Return the list of headers and the complete regex (anchored with ^ and $)
    return {headers, "^" + regex_pattern + "$"};
}

// Main processing function
void process_log(const std::string& log_file_path, const std::string& log_format_string, const std::string& output_base_dir = "output") {
    // 1. Generate the regular expression
    auto [headers, regex_str] = generate_logformat_regex(log_format_string);
    if (headers.empty()) {
        throw std::runtime_error("Could not parse any headers from log_format.");
    }
    std::cout << "[INFO] Generating regex for format: " << log_format_string << std::endl;
    std::cout << "[INFO] Generated Regex: " << regex_str << std::endl;

    // 2. Compile the PCRE2 regular expression
    pcre2_code* re;
    int error_code;
    PCRE2_SIZE error_offset;
    re = pcre2_compile((PCRE2_SPTR)regex_str.c_str(), PCRE2_ZERO_TERMINATED, 0, &error_code, &error_offset, NULL);
    if (re == NULL) {
        PCRE2_UCHAR buffer[256];
        pcre2_get_error_message(error_code, buffer, sizeof(buffer));
        throw std::runtime_error("PCRE2 compilation failed at offset " + std::to_string(error_offset) + ": " + (char*)buffer);
    }

    // 3. Prepare input and output
    fs::path input_path(log_file_path);
    if (!fs::exists(input_path)) {
        throw std::runtime_error("Input log file not found: " + log_file_path);
    }
    // Get "HDFS" from "Logs/HDFS/HDFS.log"
    std::string log_name_stem = input_path.stem().string(); 
    fs::path output_dir = fs::path(output_base_dir) / log_name_stem;
    fs::create_directories(output_dir);
    std::cout << "[INFO] Processing log file: " << log_file_path << std::endl;
    std::cout << "[INFO] Output directory created/ensured: " << output_dir.string() << std::endl;

    // 4. Create an output file stream for each header
    std::map<std::string, std::ofstream> output_files;
    for (const auto& header : headers) {
        output_files[header].open(output_dir / (header + ".txt"));
    }

    // 5. Read the log file line by line and process
    std::ifstream log_file(log_file_path);
    std::string line;
    int line_num = 0;
    pcre2_match_data* match_data = pcre2_match_data_create_from_pattern(re, NULL);

    while (std::getline(log_file, line)) {
        line_num++;
        int rc = pcre2_match(re, (PCRE2_SPTR)line.c_str(), line.length(), 0, 0, match_data, NULL);

        if (rc >= 0) { // Match successful
            std::cout << "[INFO] Processing line " << line_num << "... Matched." << std::endl;
            PCRE2_SIZE* ovector = pcre2_get_ovector_pointer(match_data);
            for (const auto& header : headers) {
                PCRE2_SPTR substring_start;
                int name_entry_size = pcre2_substring_get_byname(match_data, (PCRE2_SPTR)header.c_str(), &substring_start);
                if (name_entry_size >= 0) {
                    output_files[header] << std::string((const char*)substring_start, name_entry_size) << std::endl;
                    pcre2_substring_free(substring_start);
                } else {
                     output_files[header] << std::endl; // In theory, the named group will be found, but just in case
                }
            }
        } else { // Match failed
            std::cout << "[WARN] Processing line " << line_num << "... No match. Treating entire line as content." << std::endl;
            for (const auto& header : headers) {
                if (header == "Content") {
                    output_files[header] << line << std::endl;
                } else {
                    output_files[header] << std::endl; // Write an empty line for other fields
                }
            }
        }
    }

    // 6. Clean up
    pcre2_match_data_free(match_data);
    pcre2_code_free(re);
    // ofstream will automatically close the files on destruction

    std::cout << "[SUCCESS] Log processing finished. Output is in " << output_dir.string() << std::endl;
}

int main() {
    // --- Example Usage ---
    // Modify the two lines below to process different log files and formats
    std::string log_file = "Logs/HDFS/HDFS.log";
    std::string log_format = "<Date> <Time> <Pid> <Level> <Component>: <Content>";

    // Make sure you have created the corresponding log file, e.g., Logs/HDFS/HDFS.log,
    // and placed the log content inside it.

    try {
        process_log(log_file, log_format);
    } catch (const std::exception& e) {
        std::cerr << "[FATAL] An exception occurred: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}