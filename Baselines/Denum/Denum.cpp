/*
Denum C++ implemention.

Part One: elastic encoder/decoder
Part two: Pure number/Tokens containing only numbers and special characters processing
Part three: Numeric variable processing
Part four: Block compression implementation
Part five: Main function with experiment logging

*/


#define PCRE2_CODE_UNIT_WIDTH 8
#include <iostream>
#include <unordered_set>
#include <string>
#include <vector>
#include <regex>
#include <unordered_map>
#include <list>
#include <fstream>
#include <sys/stat.h>
#include <thread>
#include <mutex>
#include <chrono>
#include <pcre2.h>
#include <stdexcept>
#include <future>
#include <filesystem>
#include <cstdlib>
#include <algorithm>
#include <queue>
#include <cmath>
#include <iomanip>

std::queue<std::string> lines;
std::mutex mtx;


/*
Part One: elastic encoder/decoder
*/
int64_t zigzag_encode(int64_t num) {
    return (num << 1) ^ (num >> 63);
}

int64_t zigzag_decode(int64_t num) {
    return (num >> 1) ^ -(num & 1);
}

std::vector<unsigned char> elastic_encode(int64_t num) {
    std::vector<unsigned char> buffer;
    uint64_t cur = zigzag_encode(num);
    while (true) {
        if (cur < 0x80) {
            buffer.push_back(static_cast<unsigned char>(cur));
            break;
        } else {
            buffer.push_back(static_cast<unsigned char>((cur & 0x7F) | 0x80));
            cur >>= 7;
        }
    }
    return buffer;
}

int64_t elastic_decode(const std::vector<unsigned char>& num_bytes) {
    int64_t ret = 0;
    int offset = 0;
    for (auto cur : num_bytes) {
        ret |= (static_cast<int64_t>(cur & 0x7F) << offset);
        if ((cur & 0x80) == 0) {
            break;
        }
        offset += 7;
    }
    return zigzag_decode(ret);
}

std::vector<int64_t> elastic_decode_bytes(const std::vector<unsigned char>& binary_bytes) {
    std::vector<int64_t> num_list;
    std::vector<unsigned char> num_byte;
    for (auto byt : binary_bytes) {
        num_byte.push_back(byt);
        if (byt < 128) {
            int64_t decode_num = elastic_decode(num_byte);
            num_list.push_back(decode_num);
            num_byte.clear();
        }
    }
    return num_list;
}


/*
Part two: Pure number/Tokens containing only numbers and special characters processing
*/
struct RegexPattern {
    std::vector<std::string> patterns;
    std::vector<std::string> substitutions;
};

class LogProcessor {
public:
    std::string logname;
    std::unordered_map<std::string, RegexPattern> regex_map;
    std::vector<pcre2_code *> compiled_patterns;
    pcre2_code *re_num;

    LogProcessor(const std::string &name) : logname(name) {
        compile_num(&re_num, R"((?<![a-zA-Z0-9])\d+(?![a-zA-Z0-9]))");
        regex_map["Android"] = { {R"((\d+)\.(\d+)\.(\d+)\.(\d+))", R"((\d+)-(\d+) (\d+):(\d+):(\d+)(?:\.(\d+))?)"}, {"<I>", "<T>"} };
        regex_map["Apache"] = { {R"((\d+)\.(\d+)\.(\d+)\.(\d+))", R"((\d{2}) (\d+):(\d+):(\d+))"}, {"<I>", "<T>"} };
        regex_map["BGL"] = { {R"((\d+)-(\d+)-(\d+)-(\d+)\.(\d+)\.(\d+))", R"((\d+):(\d+):(\d+))",R"((\d+)\.(\d+)\.(\d+))"}, {"<E>", "<T>", "<F>"} };
        regex_map["Hadoop"] = { {R"((\d+)\-(\d+)\-(\d+))", R"((\d+):(\d+):(\d+),(\d+))"}, {"<D>", "<T>"} };
        regex_map["HDFS"] = { { R"((\d+)\.(\d+)\.(\d+)\.(\d+))", R"((\d+):(\d+):(\d+),(\d+))"}, {"<I>", "<T>"} };
        regex_map["HealthApp"] = { { R"((\d+):(\d+):(\d+):(\d+))"}, {"<T>"} };
        regex_map["HPC"] = { {R"((\d+)\.(\d+)\.(\d+)\.(\d+))", R"((\d+)-(\d+) (\d+):(\d+):(\d+)(?:\.(\d+))?)"}, {"<I>", "<T>"} };
        regex_map["Linux"] = { {R"((\d+)\.(\d+)\.(\d+)\.(\d+))", R"((\d+)-(\d+) (\d+):(\d+):(\d+)(?:\.(\d+))?)"}, {"<I>", "<T>"} };
        regex_map["Mac"] = { {R"((\d+)-(\d+)-(\d+)-(\d+))", R"((\d+):(\d+):(\d+)(?:\.(\d+))?)"}, {"<D>", "<T>"} };
        regex_map["OpenSSH"] = { {R"((\d+)\.(\d+)\.(\d+)\.(\d+))", R"((\d+) (\d+):(\d+):(\d+)(?:\.(\d+))?)",R"(sshd<span data-type="block-math" data-value="KFxkKyk="></span>:)"}, {"<I>", "<T>", "<S>"} };
        regex_map["OpenStack"] = { { R"(\.(\d+)-(\d+)-(\d+)_(\d+):(\d+):(\d+))",R"((\d+)-(\d+)-(\d+).(\d+):(\d+):(\d+)\.(\d+))"}, { "<D>", "<T>"} };
        regex_map["Proxifier"] = { { R"((\d+)\.(\d+) (\d+):(\d+):(\d+)(?:\.(\d+))?)"}, {"<T>"} };
        regex_map["Spark"] = { { R"((\d+)\.(\d+)\.(\d+)\.(\d+))", R"((\d{2})\/(\d{2})\/(\d{2}) (\d+):(\d+):(\d+))",R"((\d+)\.(\d{1}) MB)",R"((\d+)\.(\d{1}) KB)",R"((\d+)\.(\d{1}) GB)",R"((\d+)\.(\d{1}) B)"}, {"<I>", "<T>", "<M>", "<K>", "<G>", "<B>"} };
        regex_map["Thunderbird"] = { { R"((\d+)\.(\d+)\.(\d+)\.(\d+))",R"((\d+):(\d+):(\d+))",R"((\d{4}})\.(\d+)\.(\d+))",R"(<span data-type="block-math" data-value="KFxkKyk="></span>:)"}, {"<I>","<T>","<A>","<B>"}};
        regex_map["Windows"] = { {  R"((\d+)\.(\d+)\.(\d+)\.(\d+))",R"((\d+)-(\d+)-(\d+) (\d+):(\d+):(\d+))", R"((\d+):(\d+):(\d+))"}, {"<I>","<T>","<D>"} };
        regex_map["Zookeeper"] = { {  R"((\d+)\.(\d+)\.(\d+)\.(\d+))",R"((\d+)-(\d+)-(\d+) (\d+):(\d+):(\d+),(\d+))", R"((\d+):(\d+):(\d+))"}, {"<I>","<T>","<D>"} };
        regex_map["bytedance"] = { { R"((\d+)-(\d+)-(\d+) (\d+):(\d+):(\d+))"}, {"<T>"} };

        if (regex_map.find(logname) != regex_map.end()) {
            const auto &patterns = regex_map[logname].patterns;
            for (const auto &pattern : patterns) {
                pcre2_code *re = compile_pattern(pattern.c_str());
                compiled_patterns.push_back(re);
            }
        } else {
            std::cerr << "Warning: No predefined patterns for logname '" << logname << "'." << std::endl;
        }
    }

    ~LogProcessor() {
        for (auto re : compiled_patterns) {
            pcre2_code_free(re);
        }
        pcre2_code_free(re_num);
    }

    std::pair<std::vector<std::string>, std::unordered_map<std::string, std::list<int64_t>>> replace_and_group(const std::vector<std::string> &lst) {
        std::unordered_map<std::string, std::list<int64_t>> patterns;
        std::vector<std::string> replaced;
        const std::string alpha = "abcdefghijklmnopqrstuvwxyz";
        const auto &substitutions = regex_map[logname].substitutions;

        for (auto &item : lst) {
            std::string result = item;
            for (size_t i = 0; i < compiled_patterns.size(); ++i) {
                result = process_with_pattern(result, compiled_patterns[i], substitutions[i], patterns);
            }
            result = process_with_pattern(result, re_num, alpha, patterns, true);
            replaced.push_back(result);
        }
        return {replaced, patterns};
    }

private:
    pcre2_code* compile_pattern(const char *pattern) {
        int errornumber;
        PCRE2_SIZE erroroffset;
        pcre2_code *re = pcre2_compile((PCRE2_SPTR)pattern, PCRE2_ZERO_TERMINATED, 0, &errornumber, &erroroffset, nullptr);
        if (re == nullptr) throw std::runtime_error("Regex compilation failed");
        return re;
    }

    void compile_num(pcre2_code **re, const char *pattern) {
        int errornumber;
        PCRE2_SIZE erroroffset;
        *re = pcre2_compile((PCRE2_SPTR)pattern, PCRE2_ZERO_TERMINATED, 0, &errornumber, &erroroffset, nullptr);
        if (*re == nullptr) throw std::runtime_error("Regex compilation failed");
    }

    std::string process_with_pattern(const std::string &input, pcre2_code *re, const std::string &substitution, std::unordered_map<std::string, std::list<int64_t>> &patterns, bool is_num = false) {
        std::string result;
        PCRE2_SIZE last_pos = 0;
        PCRE2_SPTR subject = (PCRE2_SPTR)input.c_str();
        size_t subject_length = strlen((char *)subject);
        pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(re, nullptr);
        int rc;
        while ((rc = pcre2_match(re, subject, subject_length, last_pos, 0, match_data, nullptr)) > 0) {
            PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(match_data);
            if (is_num) {
                std::string num = input.substr(ovector[0], ovector[1] - ovector[0]);
                size_t len = num.length();
                if (len < 15) {
                    std::string pattern_key = "<" + std::string(1, substitution[len - 1]) + (len >= 4 ? std::string(1, substitution[num[0] - '0']) : "") + ">";
                    patterns[pattern_key].push_back(std::stoll(num));
                    result += input.substr(last_pos, ovector[0] - last_pos) + pattern_key;
                } else {
                    result += input.substr(last_pos, ovector[0] - last_pos) + num;
                }
            } else {
                std::string match = input.substr(ovector[0], ovector[1] - ovector[0]);
                std::string num_str;
                for (char c : match) {
                    if (std::isdigit(c)) {
                        num_str += c;
                    }
                }
                std::string pattern_key = substitution;
                patterns[pattern_key].push_back(std::stoll(num_str));
                result += input.substr(last_pos, ovector[0] - last_pos) + pattern_key;
            }
            last_pos = ovector[1];
        }
        result += input.substr(last_pos);
        pcre2_match_data_free(match_data);
        return result;
    }
};

/*
Part Three: Numeric variable processing
*/
class DenumLogProcessor {
private:
    std::string logname;
    std::string output_dir_base;

public:
    DenumLogProcessor(std::string name, std::string out_dir) : logname(name), output_dir_base(out_dir) {}

    std::vector<std::string> variable_extract(const std::vector<std::string>& logs, const std::string& chunkID) {
        std::vector<std::string> modified_lines;
        std::vector<std::string> variable_set;
        std::regex digit_pattern("\\d");
        std::regex regex_pattern;
        std::vector<std::string> delimiters;
        std::tie(regex_pattern, delimiters) = delimeter_mining(logs);
        std::string modified_line;
        for (const auto& log : logs) {
            modified_line = "";
            std::vector<std::string> split = split_by_multiple_delimiters(regex_pattern, log, true);
            for (const auto& word : split) {
                if (std::regex_search(word, digit_pattern)) {
                    modified_line += "<*>";
                    variable_set.push_back(word);
                } else {
                    modified_line += word;
                }
            }
            modified_lines.push_back(modified_line);
        }
        store_content_with_ids(variable_set, "variableset", chunkID, "lzma");
        return modified_lines;
    }

    void ensure_directory_exists(const std::string& dir) {
        struct stat buffer;
        if (stat(dir.c_str(), &buffer) != 0) {
            std::filesystem::create_directories(dir);
        }
    }

    void store_content_with_ids(const std::vector<std::string>& input, const std::string& output, const std::string& chunkID, const std::string& compressor) {
        std::unordered_map<std::string, int> content_to_id;
        std::unordered_map<int, std::string> id_to_content;
        int id_counter = 1;
        std::vector<int> id_list;
        std::string id_dir = output_dir_base + "/" + chunkID + "/";
        std::string ids_file_path = id_dir + logname + output + "ids.bin";
        std::string mapping_file_path = id_dir + logname + output + "mapping.txt";
        ensure_directory_exists(id_dir);

        for (const auto& line : input) {
            if (line.empty()) continue;
            if (content_to_id.find(line) == content_to_id.end()) {
                content_to_id[line] = id_counter;
                id_to_content[id_counter] = line;
                id_counter++;
            }
            id_list.push_back(content_to_id[line]);
        }

        std::vector<std::pair<int, std::string>> sorted_content(id_to_content.begin(), id_to_content.end());
        std::sort(sorted_content.begin(), sorted_content.end(), [](const auto& a, const auto& b) {
            return a.first < b.first;
        });

        std::ofstream ids_file(ids_file_path, std::ios::binary);
        std::ofstream mapping_file(mapping_file_path);

        for (const auto& pair : sorted_content) {
            mapping_file << pair.second << "\n";
        }
        for (int id : id_list) {
            auto encoded = elastic_encode(id);
            ids_file.write(reinterpret_cast<const char*>(encoded.data()), encoded.size());
        }
        ids_file.close();
        mapping_file.close();
    }

    std::string regex_escape(const std::string& pattern) {
        static const std::string special_chars = R"([-[\]{}()*+?.\\^$|])";
        std::string escaped_pattern;
        for (char c : pattern) {
            if (special_chars.find(c) != std::string::npos) {
                escaped_pattern += '\\';
            }
            escaped_pattern += c;
        }
        return escaped_pattern;
    }

    std::tuple<std::regex, std::vector<std::string>> delimeter_mining(const std::vector<std::string>& logs) {
        std::vector<std::string> temp = logs;
        std::random_shuffle(temp.begin(), temp.end());
        std::unordered_set<size_t> lengths;
        std::vector<std::string> sample;
        size_t iteration_count = 0;
        for (const auto& log : temp) {
            size_t log_len = log.size();
            if (lengths.find(log_len) == lengths.end()) {
                lengths.insert(log_len);
                sample.push_back(log);
            }
            if (lengths.size() >= 10 || ++iteration_count >= 200) break;
        }
        std::vector<std::string> delimiters = find_special_chars_with_high_freq(sample);
        if (delimiters.empty()) throw std::runtime_error("No delimiters found.");

        std::string pattern_str = "(";
        for (const auto& delimiter : delimiters) {
            if (!delimiter.empty()) pattern_str += regex_escape(delimiter) + "|";
        }
        if (pattern_str.back() == '|') pattern_str.pop_back();
        pattern_str += ")";
        if (pattern_str == "()") throw std::runtime_error("Invalid regex pattern: " + pattern_str);
        return {std::regex(pattern_str), delimiters};
    }

    std::vector<std::string> split_by_multiple_delimiters(const std::regex& pattern, const std::string& str, bool include_delimiters) {
        std::vector<std::string> result;
        auto words_begin = std::sregex_iterator(str.begin(), str.end(), pattern);
        auto words_end = std::sregex_iterator();
        size_t last_pos = 0;
        for (auto iter = words_begin; iter != words_end; ++iter) {
            std::smatch match = *iter;
            if (match.position() > last_pos) result.push_back(str.substr(last_pos, match.position() - last_pos));
            if (include_delimiters) result.push_back(match.str());
            last_pos = match.position() + match.length();
        }
        if (last_pos < str.length()) result.push_back(str.substr(last_pos));
        return result;
    }

    std::vector<std::string> find_special_chars_with_high_freq(const std::vector<std::string>& str_list, size_t freq_threshold = 10) {
        std::vector<char> candidates = {',', ' ', '|', ';', '[', ']', '(', ')', '_', '/'};
        std::unordered_map<char, size_t> char_counter;
        for (const auto& s : str_list) {
            for (char c : s) {
                if (std::find(candidates.begin(), candidates.end(), c) != candidates.end()) char_counter[c]++;
            }
        }
        std::vector<std::string> result;
        for (const auto& pair : char_counter) {
            if (pair.second > freq_threshold) result.push_back(std::string(1, pair.first));
        }
        if (result.empty()) result.push_back(" ");
        return result;
    }
};

/*
Part Four: Block compression implementation
*/
void ensure_directory_exists(const std::string& dir) {
    try {
        std::filesystem::create_directories(dir);
    } catch (const std::exception& e) {
        std::cerr << "Error creating directory: " << e.what() << std::endl;
    }
}

std::string sanitize_filename(std::string filename) {
    std::replace(filename.begin(), filename.end(), '<', '_');
    std::replace(filename.begin(), filename.end(), '>', '_');
    return filename;
}

std::list<int64_t> delta_transform(const std::list<int64_t>& num_list) {
    if (num_list.empty()) return {};
    std::list<int64_t> new_list;
    auto it = num_list.begin();
    int64_t last = *it;
    new_list.push_back(last);
    for (++it; it != num_list.end(); ++it) {
        int64_t delta = *it - last;
        new_list.push_back(delta);
        last = *it;
    }
    return new_list;
}

void compressDirectory(const std::string& output_dir, int block_id) {
    std::string directoryPath = output_dir + "/" + std::to_string(block_id);
    std::string archivePath = output_dir + "/compressed" + std::to_string(block_id) + ".tar.xz";
    std::string command = "tar -cJf " + archivePath + " -C " + directoryPath + " .";
    int result = std::system(command.c_str());
    if (result != 0) {
        std::cerr << "Command failed with return code: " << result << std::endl;
    } else {
        std::cout << "Block " << block_id << " directory successfully compressed into " << archivePath << std::endl;
        std::filesystem::remove_all(directoryPath);
    }
}

void processLogBlock(const std::vector<std::string>& block, int block_id, const std::string& output_dir, LogProcessor& log_processor, DenumLogProcessor& denum_processor, std::map<int, std::vector<std::string>>& final_outputs, const std::string& output_logs) {
    auto [final_output, final_patterns] = log_processor.replace_and_group(block);
    std::string logname_dir = output_dir + "/" + std::to_string(block_id) + "/";
    ensure_directory_exists(logname_dir);
    std::vector<std::string> modified_logs = denum_processor.variable_extract(final_output, std::to_string(block_id));
    if (output_logs == "1") {
        denum_processor.store_content_with_ids(modified_logs, "all", std::to_string(block_id), "lzma");
    } else if (output_logs == "2") {
        final_outputs[block_id] = modified_logs;
    } else if (output_logs == "3") {
        std::ofstream final_log_file(logname_dir + "logswithoutnums.log");
        if (final_log_file.is_open()) {
            for (const auto& log : modified_logs) {
                final_log_file << log << std::endl;
            }
            final_log_file.close();
        } else {
            std::cerr << "Unable to open final log file for writing: " << logname_dir + "logswithoutnums.log" << std::endl;
        }
    }

    for (const auto &pair : final_patterns) {
        std::string sanitized_filename = sanitize_filename(pair.first);
        std::string filename = logname_dir + sanitized_filename + ".bin";
        std::ofstream file(filename, std::ios::out | std::ios::binary);
        std::list<int64_t> transformed;
        if (pair.first != "<I>" && pair.first != "<a>" && pair.first != "<b>" && pair.first != "<c>") {
            transformed = delta_transform(pair.second);
        } else {
            transformed = pair.second;
        }
        for (const auto& num : transformed) {
            auto encoded = elastic_encode(num);
            file.write(reinterpret_cast<const char*>(encoded.data()), encoded.size());
        }
        file.close();
    }
    compressDirectory(output_dir, block_id);
}

// ======================= NEW FEATURE: EXPERIMENT RESULT LOGGING =======================

// A static mutex to ensure that writing to the result file is thread-safe,
// preventing data corruption if multiple instances of the program run concurrently.
static std::mutex result_file_mutex;

/**
 * @brief Logs the results of a compression experiment to a CSV file.
 *
 * @param logname The name of the dataset.
 * @param num_threads The number of threads used for compression.
 * @param block_size The size of each log block.
 * @param original_size_mb The original size of the log file in megabytes.
 * @param compressed_size_mb The final compressed size in megabytes.
 * @param compression_ratio The calculated compression ratio.
 * @param execution_time_ms The total execution time in milliseconds.
 * @param compression_speed_mbps The compression speed in MB/s.
 */
void log_experiment_result(
    const std::string& logname,
    int num_threads,
    size_t block_size,
    double original_size_mb,
    double compressed_size_mb,
    double compression_ratio,
    long long execution_time_ms,
    double compression_speed_mbps
) {
    std::lock_guard<std::mutex> lock(result_file_mutex);
    const std::string result_filename = "compression_results.csv";
    bool file_exists = std::filesystem::exists(result_filename);
    std::ofstream result_file(result_filename, std::ios::app);
    if (!result_file.is_open()) {
        std::cerr << "Error: Could not open experiment results file: " << result_filename << std::endl;
        return;
    }
    if (!file_exists || result_file.tellp() == 0) {
        result_file << "Timestamp,LogName,NumThreads,BlockSize,OriginalSize_MB,CompressedSize_MB,CompressionRatio,ExecutionTime_ms,CompressionSpeed_MBps\n";
    }
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    #ifdef _MSC_VER
        struct tm tm_buf;
        localtime_s(&tm_buf, &in_time_t);
        ss << std::put_time(&tm_buf, "%Y-%m-%d %X");
    #else
        ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %X");
    #endif

    result_file << ss.str() << ","
                << logname << ","
                << num_threads << ","
                << block_size << ","
                << std::fixed << std::setprecision(4) << original_size_mb << ","
                << std::fixed << std::setprecision(4) << compressed_size_mb << ","
                << std::fixed << std::setprecision(3) << compression_ratio << ","
                << execution_time_ms << ","
                << std::fixed << std::setprecision(3) << compression_speed_mbps << "\n";
    
    result_file.close();
    std::cout << "Experiment results logged to " << result_filename << std::endl;
}

// ======================= END OF NEW FEATURE =======================


/*
Part Five: main function
*/
int main(int argc, char* argv[]) {
    if (argc < 5) {
        std::cerr << "Usage: " << argv[0] << " <logname> <block_size> <mode> <num_threads>" << std::endl;
        return 1;
    }

    const std::string logname = argv[1];
    size_t BLOCK_SIZE;
    const std::string output_logs = argv[3];
    int num_threads;

    try {
        BLOCK_SIZE = std::stoul(argv[2]);
        num_threads = std::stoi(argv[4]);
        if (num_threads <= 0) {
            throw std::invalid_argument("Number of threads must be positive.");
        }
    } catch (const std::exception& e) {
        std::cerr << "Invalid command-line argument: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "Block Size: " << BLOCK_SIZE << std::endl;
    std::cout << "Using " << num_threads << " concurrent threads." << std::endl;
    
    auto start = std::chrono::high_resolution_clock::now();
    const std::string log_path = "../../Logs/" + logname + "/" + logname + ".log";
    const std::string base_output_dir = "Baseline/Denum/results/" + logname;
    
    std::vector<std::future<void>> futures;
    std::map<int, std::vector<std::string>> final_outputs;

    LogProcessor log_processor(logname);
    DenumLogProcessor denum_processor(logname, base_output_dir);

    std::ifstream log_file(log_path);
    if (!log_file.is_open()) {
        std::cerr << "Unable to open log file: " << log_path << std::endl;
        return 1;
    }

    if (std::filesystem::exists(base_output_dir)) {
        std::filesystem::remove_all(base_output_dir);
    }
    ensure_directory_exists(base_output_dir);

    std::vector<std::string> block;
    block.reserve(BLOCK_SIZE);
    std::string line;
    int block_index = 0;

    while (std::getline(log_file, line)) {
        block.push_back(line);
        if (block.size() == BLOCK_SIZE) {
            futures.push_back(std::async(std::launch::async, processLogBlock, block, block_index, base_output_dir, std::ref(log_processor), std::ref(denum_processor), std::ref(final_outputs), output_logs));
            block.clear();
            ++block_index;
            
            if (futures.size() >= static_cast<size_t>(num_threads)) {
                for(auto& fut : futures) {
                    fut.wait();
                }
                futures.clear();
            }
        }
    }

    if (!block.empty()) {
        futures.push_back(std::async(std::launch::async, processLogBlock, block, block_index, base_output_dir, std::ref(log_processor), std::ref(denum_processor), std::ref(final_outputs), output_logs));
    }

    for (auto& future : futures) {
        future.wait();
    }

    auto stop = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start);
    
    uintmax_t originalFileSize = std::filesystem::file_size(log_path);
    double dataSizeInMB = static_cast<double>(originalFileSize) / (1024.0 * 1024.0);
    double speedInMBPerSecond = (duration.count() > 0) ? (dataSizeInMB / (duration.count() / 1000.0)) : 0;
    
    uintmax_t totalBytes = 0;
    for (int i = 0; i <= block_index; ++i) {
        std::string compressed_path = base_output_dir + "/compressed" + std::to_string(i) + ".tar.xz";
        if (std::filesystem::exists(compressed_path)) {
             totalBytes += std::filesystem::file_size(compressed_path);
        }
    }
    double totalSizeMB = static_cast<double>(totalBytes) / (1024.0 * 1024.0);
    double CR = (totalSizeMB > 0) ? (dataSizeInMB / totalSizeMB) : 0;

    std::cout << "Replacement completed in " << duration.count() << " milliseconds." << std::endl;
    std::cout << "Compression speed: " << std::fixed << std::setprecision(3) << speedInMBPerSecond << " MB/s" << std::endl;
    std::cout << "Achieved size: " << totalBytes << " Bytes" << std::endl;
    std::cout << "Compression ratio: " << std::fixed << std::setprecision(3) << CR << ":1" << std::endl;
    
    // Log the final results to the CSV file.
    log_experiment_result(
        logname,
        num_threads,
        BLOCK_SIZE,
        dataSizeInMB,
        totalSizeMB,
        CR,
        duration.count(),
        speedInMBPerSecond
    );
    
    if (output_logs == "2") {
        std::ofstream final_log_file(std::filesystem::path(base_output_dir).parent_path() / (logname + ".log"));
        for (const auto& [block_id, output] : final_outputs) {
            for (const auto& log : output) {
                final_log_file << log << std::endl;
            }
        }
        final_log_file.close();
    }
    return 0;
}