#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <fstream>
#include <filesystem>
#include <unordered_map>
#include <array>
#include <mutex>
#include <memory>
#include <algorithm>
#include <stdexcept>
#include <chrono>
#include <future>
#include <thread>
#include <iomanip>
#include <list>
#include <set>
#include <unordered_set>
#include <tuple>
#include <optional>
#include <iterator> // For std::make_move_iterator
#include <sstream>  // For std::stringstream
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include <cctype>

// ===================================================================
// Log Processing Mode Definition
// ===================================================================
enum class LogMode { TEXT, JSON };

std::optional<LogMode> parse_log_mode(const std::string& s) {
    if (s == "text") return LogMode::TEXT;
    if (s == "json") return LogMode::JSON;
    return std::nullopt;
}

// ===================================================================
// Processing Mode Definition (Normal vs Fast)
// ===================================================================
enum class ProcessingMode { NORMAL, FAST };

std::optional<ProcessingMode> parse_processing_mode(const std::string& s) {
    if (s == "normal") return ProcessingMode::NORMAL;
    if (s == "fast") return ProcessingMode::FAST;
    return std::nullopt;
}

// ===================================================================
// Compression Kernel Definition
// ===================================================================
enum class CompressionKernel { LZMA, GZIP, BZIP2, LZ4, NONE };

struct CompressionInfo {
    std::string tar_flag;
    std::string extension;
};

CompressionInfo get_compression_info(CompressionKernel kernel) {
    switch (kernel) {
        case CompressionKernel::LZMA:   return {"-cJf", ".tar.xz"};
        case CompressionKernel::GZIP:   return {"-czf", ".tar.gz"};
        case CompressionKernel::BZIP2:  return {"-cjf", ".tar.bz2"};
        case CompressionKernel::LZ4:    return {"-c --lz4 -f", ".tar.lz4"};
        case CompressionKernel::NONE:   return {"-cf", ".tar"};
    }
    return {"-cJf", ".tar.xz"}; // Default fallback
}

std::optional<CompressionKernel> parse_kernel_from_string(const std::string& s) {
    if (s == "lzma") return CompressionKernel::LZMA;
    if (s == "gzip") return CompressionKernel::GZIP;
    if (s == "bzip2") return CompressionKernel::BZIP2;
    if (s == "lz4") return CompressionKernel::LZ4;
    if (s == "none") return CompressionKernel::NONE;
    return std::nullopt;
}

// ===================================================================
// Helper Functions
// ===================================================================
void ensure_directory_exists(const std::string& dir) {
    try {
        std::filesystem::create_directories(dir);
    } catch (const std::exception& e) {
        std::cerr << "Error creating directory: " << e.what() << std::endl;
    }
}

struct TokenType {
    bool has_alpha = false;
    bool has_digit = false;
    bool has_special = false;
    bool is_pure_digit = false;
};

TokenType get_token_type(std::string_view s) {
    TokenType type;
    if (s.empty()) {
        return type;
    }
    bool might_be_pure_digit = true;
    for (char c : s) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (std::isdigit(static_cast<unsigned char>(c))) {
            type.has_digit = true;
        } else {
            might_be_pure_digit = false;
            if (std::isalpha(uc) || uc == '<' || uc == '>') {
                type.has_alpha = true;
            } else {
                type.has_special = true;
            }
            if (std::isalpha(static_cast<unsigned char>(c))) {
                type.has_alpha = true;
            } else {
                type.has_special = true;
            }
        }
    }
    if (might_be_pure_digit && type.has_digit) {
        type.is_pure_digit = true;
    }
    return type;
}

std::string extract_special_chars(std::string_view s) {
    std::string special;
    special.reserve(s.length());
    for (char c : s) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (uc < 128 && !std::isalnum(uc) && !std::isspace(uc)) {
            special += c;
        }
    }
    return special;
}

void append_escaped(std::string& tag, char c) {
    if (c == '.' || c == '*' || c == '+' || c == '?' || c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c == '}' || c == '^' || c == '$' || c == '\\' ) {
        tag.push_back('\\');
    }
    tag.push_back(c);
}

std::string generate_regex_like_tag(std::string_view s) {
    if (s.empty()) return "";
    std::string tag;
    tag.reserve(s.length() + 10);
    size_t digit_count = 0;
    for (char c : s) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            digit_count++;
        } else {
            if (digit_count > 0) {
                tag.append("\\d{").append(std::to_string(digit_count)).append("}");
                digit_count = 0;
            }
            append_escaped(tag, c);
        }
    }
    if (digit_count > 0) {
        tag.append("\\d{").append(std::to_string(digit_count)).append("}");
    }
    return tag;
}

inline bool is_delimiter(char c) {
    static const auto delimiters = [] {
        std::array<bool, 256> d{};
        const std::string delim_chars = " \t\n\r,|;[]_+=:@#\\\"{}()";
        for (unsigned char ch : delim_chars) {
            d[ch] = true;
        }
        return d;
    }();
    return delimiters[static_cast<unsigned char>(c)];
}

bool has_delimiter(std::string_view sv) {
    for (char c : sv) {
        if (is_delimiter(c)) {
            return true;
        }
    }
    return false;
}

std::string to_alpha_id(int64_t n, const std::string& prefix = "z") {
    if (n < 0) return prefix + "neg" + to_alpha_id(-n, "");
    if (n == 0) return prefix + "a";
    const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    const int charset_size = sizeof(charset) - 1;
    std::string result;
    while (n > 0) {
        result += charset[n % charset_size];
        n /= charset_size;
    }
    std::reverse(result.begin(), result.end());
    return prefix + result;
}

std::vector<unsigned char> elastic_encode(int64_t num) {
    std::vector<unsigned char> buffer;
    uint64_t cur = (num << 1) ^ (num >> 63);
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

std::list<int64_t> delta_transform(const std::list<int64_t>& l) {
    if (l.empty()) return {};
    std::list<int64_t> new_list;
    auto it = l.begin();
    int64_t initial = *it;
    new_list.push_back(initial);
    int64_t last = initial;
    for (++it; it != l.end(); ++it) {
        new_list.push_back(*it - last);
        last = *it;
    }
    return new_list;
}

// In-memory dictionary and ID data
struct StoredContent {
    std::string mapping_data;
    std::vector<unsigned char> ids_data;
};

// Generate in-memory dictionary and ID data from a vector of strings
StoredContent generate_stored_content(const std::vector<std::string>& lines) {
    std::unordered_map<std::string, int> content_to_id;
    std::vector<std::pair<int, std::string>> id_to_content;
    int id_counter = 1;
    std::vector<int> id_list;
    id_list.reserve(lines.size());

    for (const auto& line : lines) {
        // An empty string should not enter the dictionary but may exist as a variable value, represented by ID 0.
        if (line.empty()) {
            id_list.push_back(0);
            continue;
        }
        auto it = content_to_id.find(line);
        if (it == content_to_id.end()) {
            content_to_id[line] = id_counter;
            id_to_content.push_back({id_counter, line});
            id_list.push_back(id_counter++);
        } else {
            id_list.push_back(it->second);
        }
    }

    std::stringstream mapping_ss;
    std::sort(id_to_content.begin(), id_to_content.end());
    for (const auto& pair : id_to_content) {
        mapping_ss << pair.second << "\n";
    }

    std::vector<unsigned char> ids_buffer;
    for (int id : id_list) {
        auto encoded = elastic_encode(id);
        ids_buffer.insert(ids_buffer.end(), encoded.begin(), encoded.end());
    }
    
    return {mapping_ss.str(), ids_buffer};
}

// The original store_content_with_ids is reserved for Normal mode and template files.
void store_content_with_ids(const std::vector<std::string>& lines, const std::string& mapping_file_path, const std::string& ids_file_path) {
    auto content = generate_stored_content(lines);
    if (!ids_file_path.empty()){
        std::ofstream ids_file(ids_file_path, std::ios::binary);
        ids_file.write(reinterpret_cast<const char*>(content.ids_data.data()), content.ids_data.size());
    }
    if (!mapping_file_path.empty()){
        std::ofstream mapping_file(mapping_file_path);
        mapping_file << content.mapping_data;
    }
}


// ===================================================================
// Data Structures and Core Classes
// ===================================================================
enum class PieceType { STATIC_TEXT, VARIABLE };

struct LinePiece {
    PieceType type;
    std::string original_token;
    std::string full_tag;
    // For JSON: positions in original line to allow reconstruction
    std::optional<size_t> start_pos;
    std::optional<size_t> end_pos;
};

using AnalyzedLine = std::vector<LinePiece>;

std::string build_structured_tag( std::string_view context, std::string_view structure, std::optional<size_t> token_index, std::optional<size_t> token_len) {
    std::string tag = "<";
    bool first_part = true;
    auto append_part = [&](const std::string& key, const std::string& value) {
        if (value.empty()) return;
        if (!first_part) {
            tag += "|";
        }
        tag += key + "=" + value;
        first_part = false;
    };

    if (token_index) append_part("IDX", std::to_string(*token_index));
    if (token_len) append_part("LEN", std::to_string(*token_len));
    if (!context.empty()) append_part("CTX", std::string(context));
    if (!structure.empty()) {
        std::string_view inner_structure = structure;
        if (inner_structure.length() >= 2 && inner_structure.front() == '<' && inner_structure.back() == '>') {
            inner_structure = inner_structure.substr(1, inner_structure.length() - 2);
        }
        append_part("STR", std::string(inner_structure));
    }
    tag += ">";
    return tag;
}


class PatternRecognizer {
public:
    std::vector<pcre2_code*> compiled_patterns;
    std::vector<std::string> substitutions;

    PatternRecognizer(const std::string& logname) {
        std::unordered_map<std::string, RegexPattern> regex_map;
        regex_map["Android"] = { { R"((\d{2})-(\d{2}) (\d{2}):(\d{2}):(\d{2})\.(\d{3}))"}, {"<T>"} };
        regex_map["Apache"] = { {R"((\d+)\.(\d+)\.(\d+)\.(\d+))", R"((\d{2}) (\d{2}):(\d{2}):(\d{2}))"}, {"<I>", "<T>"} };
        regex_map["BGL"] = { {R"((\d{4})-(\d{2})-(\d{2})-(\d{2})\.(\d{2})\.(\d{2}))", R"((\d{4})\.(\d{2})\.(\d{2}))", R"(core\.(\d+))", R"(\.(\d{6}))"}, { "<P>", "<O>", "<Q>","<R>"} };
        regex_map["Hadoop"] = { {R"((\d{4})\-(\d{2})\-(\d{2}))", R"((\d{2}):(\d{2}):(\d{2}),(\d{3}))"}, {"<X>", "<T>"} };
        regex_map["HDFS"] = { { R"((\d+)\.(\d+)\.(\d+)\.(\d+))", R"(blk_-?\d+)"}, {"<A>",  "<B>"} };
        regex_map["HealthApp"] = { { R"((\d{8})\-(\d+):(\d+):(\d+))"}, {"<A>"} };
        regex_map["HPC"] = { {R"(\d{10})"}, {"<T>"} };
        regex_map["Linux"] = { { R"(rhost=([^\s]+))", R"((\d+)\.(\d+)\.(\d+)\.(\d+))", R"((\d{2}):(\d{2}):(\d{2}))"}, {"<A>", "<B>", "<T>"} };
        regex_map["Mac"] = { {R"((\d+)-(\d+)-(\d+)-(\d+))", R"((\d{2}):(\d{2}):(\d{2}))"}, {"<X>", "<T>"} };
        regex_map["OpenSSH"] = { {R"((\d+)\.(\d+)\.(\d+)\.(\d+)(?![.\d]))", R"((\d+) (\d{2}):(\d{2}):(\d{2}))"}, {"<A>", "<T>"} };
        regex_map["OpenStack"] = { { R"(\.(\d+)-(\d+)-(\d+)_(\d+):(\d+):(\d+))",R"((\d+)-(\d+)-(\d+).(\d+):(\d+):(\d+)\.(\d+))",R"((\d+)\.(\d+)\.(\d+)\.(\d+))"}, { "<X>", "<Y>", "<Z>"} };
        regex_map["Proxifier"] = { { R"((\d{2})\.(\d{2}) (\d{2}):(\d{2}):(\d{2}))"}, {"<T>"} };
        regex_map["Spark"] = { {  R"((\d{2})\/(\d{2})\/(\d{2}) (\d{2}):(\d{2}):(\d{2}))",R"((\d+)\.(\d+)\.(\d+)\.(\d+))"}, {"<T>","<A>"}};
        regex_map["Thunderbird"] = { { R"((\d{2}):(\d{2}):(\d{2}))",R"((\d{4}\.)(\d{2})\.(\d{2}))"}, {"<A>","<T>"}};
        regex_map["Windows"] = { { R"((\d+)\.(\d+)\.(\d+)\.(\d+))",R"((\d{4})-(\d{2})-(\d{2}) (\d{2}):(\d{2}):(\d{2}))", R"((\d+):(\d+):(\d+))"}, {"<A>","<T>","<B>"} };
        regex_map["Zookeeper"] = { { R"((\d+)\.(\d+)\.(\d+)\.(\d+))",R"((\d{4})-(\d{2})-(\d{2}) (\d{2}):(\d{2}):(\d{2}),(\d{3}))"}, {"<A>","<T>"} };
        


        if (regex_map.count(logname)) {
            const auto& log_patterns = regex_map.at(logname);
            substitutions = log_patterns.substitutions;
            for (const auto& pattern_str : log_patterns.patterns) {
                compiled_patterns.push_back(compile_pattern(pattern_str.c_str()));
            }
        } else {
            std::cerr << "Warning: No predefined patterns for logname '" << logname << "'." << std::endl;
        }
    }

    ~PatternRecognizer() {
        for (auto re : compiled_patterns) {
            pcre2_code_free(re);
        }
    }
private:
    struct RegexPattern {
        std::vector<std::string> patterns;
        std::vector<std::string> substitutions;
    };

    pcre2_code* compile_pattern(const char *pattern) {
        int errornumber;
        PCRE2_SIZE erroroffset;
        pcre2_code *re = pcre2_compile((PCRE2_SPTR)pattern, PCRE2_ZERO_TERMINATED, 0, &errornumber, &erroroffset, nullptr);
        if (re == nullptr) {
            throw std::runtime_error("Regex compilation failed for: " + std::string(pattern));
        }
        return re;
    }
};

class TagManager {
public:
    std::string get_or_create_id(const std::string& full_tag) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = tag_to_id_map_.find(full_tag);
        if (it != tag_to_id_map_.end()) {
            return it->second;
        }
        std::string new_alpha_id = to_alpha_id(next_id_++);
        tag_to_id_map_[full_tag] = new_alpha_id;
        id_to_tag_map_.push_back({new_alpha_id, full_tag});
        return new_alpha_id;
    }

    const std::unordered_map<std::string, std::string>& get_tag_to_id_map() const {
        return tag_to_id_map_;
    }

    void save_mapping(const std::string& path) const {
        std::ofstream file(path);
        std::vector<std::pair<std::string, std::string>> sorted_map = id_to_tag_map_;
        std::sort(sorted_map.begin(), sorted_map.end());
        for (const auto& pair : sorted_map) {
            file << pair.first << ":" << pair.second << "\n";
        }
    }

private:
    std::mutex mutex_;
    int64_t next_id_ = 0;
    std::unordered_map<std::string, std::string> tag_to_id_map_;
    std::vector<std::pair<std::string, std::string>> id_to_tag_map_;
};

using GlobalTagData = std::unordered_map<std::string, std::vector<std::string>>;


// ===================================================================
// JSON Parsing Logic
// ===================================================================
std::vector<std::pair<std::string_view, std::string_view>> parse_json_kvs(std::string_view line_sv) {
    std::vector<std::pair<std::string_view, std::string_view>> kvs;
    if (line_sv.length() < 2 || line_sv.front() != '{' || line_sv.back() != '}') {
        return kvs;
    }
    enum class State { PRE_KEY, IN_KEY, POST_KEY, PRE_VALUE, IN_VALUE };
    State state = State::PRE_KEY;
    size_t start = 0;
    std::string_view current_key;
    for (size_t i = 1; i < line_sv.length() - 1; ++i) {
        char c = line_sv[i];
        bool is_escaped = (i > 1 && line_sv[i-1] == '\\' && (i < 2 || line_sv[i-2] != '\\'));
        switch (state) {
            case State::PRE_KEY:
                if (c == '"') { state = State::IN_KEY; start = i + 1; }
                break;
            case State::IN_KEY:
                if (c == '"' && !is_escaped) {
                    current_key = line_sv.substr(start, i - start);
                    state = State::POST_KEY;
                }
                break;
            case State::POST_KEY:
                if (c == ':') { state = State::PRE_VALUE; }
                break;
            case State::PRE_VALUE:
                if (c == '"') {
                    state = State::IN_VALUE; start = i + 1;
                } else if (!isspace(c)) {
                    start = i;
                    while (i < line_sv.length() - 1 && line_sv[i] != ',' && line_sv[i] != '}') { i++; }
                    std::string_view value = line_sv.substr(start, i - start);
                    size_t endpos = value.find_last_not_of(" \t\r\n");
                    if(endpos != std::string_view::npos) { value = value.substr(0, endpos + 1); }
                    kvs.emplace_back(current_key, value);
                    state = State::PRE_KEY;
                    i--;
                }
                break;
            case State::IN_VALUE:
                if (c == '"' && !is_escaped) {
                    kvs.emplace_back(current_key, line_sv.substr(start, i - start));
                    state = State::PRE_KEY;
                }
                break;
        }
    }
    return kvs;
}


// ===================================================================
// Core Processing Logic Function (TEXT Mode)
// ===================================================================
std::string extract_digits(std::string_view s) {
    std::string digits;
    digits.reserve(s.length());
    for (char c : s) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            digits += c;
        }
    }
    return digits;
}

void process_sub_token_single_pass(
    std::string_view token, std::string& result_line, std::string& context,
    GlobalTagData& local_tag_data, TagManager& tag_manager, size_t& token_index,
    ProcessingMode mode) {
    if (token.empty()) return;

    token_index++;
    TokenType type = get_token_type(token);
    std::string full_tag;
    bool is_variable = false;
    std::string value_to_store = std::string(token);

    if (type.is_pure_digit || (type.has_digit && !type.has_alpha) || (type.has_digit && type.has_alpha) || type.has_special) {
        is_variable = true;
        if (type.is_pure_digit) {
            if (token.length() <= 2) {
                full_tag = build_structured_tag("", "", std::nullopt, token.length());
            } else {
                full_tag = build_structured_tag(context, generate_regex_like_tag(token), token_index, std::nullopt);
            }
        } else {
            std::string special_chars_str = extract_special_chars(token);
            full_tag = build_structured_tag(context, "_" + special_chars_str, std::nullopt, std::nullopt);
        }
        
        if (mode == ProcessingMode::NORMAL) {
            if(type.is_pure_digit) {
                value_to_store = extract_digits(token);
            }
        }
    }

    if (is_variable) {
        std::string compact_id = tag_manager.get_or_create_id(full_tag);
        result_line.append("<").append(compact_id).append(">");
        
        local_tag_data[full_tag].push_back(std::move(value_to_store));
    } else {
        result_line.append(token);
        context = token;
    }
}

std::string process_line_text_single_pass(
    std::string_view line_sv, GlobalTagData& local_tag_data,
    PatternRecognizer& recognizer, TagManager& tag_manager,
    ProcessingMode mode) {
    std::string current_line(line_sv);
    std::string result_line;
    result_line.reserve(current_line.length());
    size_t token_index = 0;
    std::string context;

    for (size_t i = 0; i < recognizer.compiled_patterns.size(); ++i) {
        pcre2_code* re = recognizer.compiled_patterns[i];
        const std::string& substitution_tag = recognizer.substitutions[i];
        std::string next_line;
        next_line.reserve(current_line.length());
        PCRE2_SIZE last_pos = 0;
        pcre2_match_data* match_data = pcre2_match_data_create_from_pattern(re, nullptr);
        
        while (pcre2_match(re, (PCRE2_SPTR)current_line.c_str(), current_line.length(), last_pos, 0, match_data, nullptr) > 0) {
            PCRE2_SIZE* ovector = pcre2_get_ovector_pointer(match_data);
            std::string_view match(current_line.data() + ovector[0], ovector[1] - ovector[0]);
            next_line.append(current_line, last_pos, ovector[0] - last_pos);
            
            std::string compact_id = tag_manager.get_or_create_id(substitution_tag);
            next_line.append("<").append(compact_id).append(">");
            
            std::string value_to_store;
            if (mode == ProcessingMode::FAST) {
                value_to_store = std::string(match);
            } else {
                if (substitution_tag == "<I>") {
                    std::string ip_part;
                    for (char c : match) {
                        if (c == '.') {
                            while (ip_part.length() < 3) ip_part.insert(0, 1, '0');
                            value_to_store += ip_part;
                            ip_part.clear();
                        } else if (isdigit(c)) {
                            ip_part += c;
                        }
                    }
                    while (ip_part.length() < 3) ip_part.insert(0, 1, '0');
                    value_to_store += ip_part;
                } else if (substitution_tag == "<X>" || substitution_tag == "<Y>" || substitution_tag == "<Z>" || substitution_tag == "<B>" || substitution_tag == "<M>" || substitution_tag == "<K>" || substitution_tag == "<G>" || substitution_tag == "<S>" || substitution_tag == "<E>" || substitution_tag == "<F>" || substitution_tag == "<A>") {
                    value_to_store = std::string(match);
                } else {
                    value_to_store = extract_digits(match);
                }
            }
            local_tag_data[substitution_tag].push_back(std::move(value_to_store));
            last_pos = ovector[1];
        }
        next_line.append(current_line, last_pos, std::string::npos);
        current_line = std::move(next_line);
        pcre2_match_data_free(match_data);
    }
    
    size_t current_pos = 0;
    while (current_pos < current_line.length()) {
        size_t delimiter_end = current_pos;
        while (delimiter_end < current_line.length() && is_delimiter(current_line[delimiter_end])) { delimiter_end++; }
        if (delimiter_end > current_pos) { result_line.append(current_line, current_pos, delimiter_end - current_pos); }
        if (delimiter_end == current_line.length()) break;

        size_t token_end = delimiter_end;
        while (token_end < current_line.length() && !is_delimiter(current_line[token_end])) { token_end++; }
        std::string_view token_sv(current_line.data() + delimiter_end, token_end - delimiter_end);

        size_t start_tag_pos = token_sv.find('<');
        if (start_tag_pos == std::string_view::npos) {
            process_sub_token_single_pass(token_sv, result_line, context, local_tag_data, tag_manager, token_index, mode);
        } else {
            size_t last_pos_in_token = 0;
            while (start_tag_pos != std::string_view::npos) {
                std::string_view sub_token = token_sv.substr(last_pos_in_token, start_tag_pos - last_pos_in_token);
                if (!sub_token.empty()) {
                    process_sub_token_single_pass(sub_token, result_line, context, local_tag_data, tag_manager, token_index, mode);
                }
                size_t end_tag_pos = token_sv.find('>', start_tag_pos);
                if (end_tag_pos == std::string_view::npos) { last_pos_in_token = start_tag_pos; break; }
                
                std::string_view tag_part = token_sv.substr(start_tag_pos, end_tag_pos - start_tag_pos + 1);
                result_line.append(tag_part);
                context = std::string(tag_part);

                last_pos_in_token = end_tag_pos + 1;
                start_tag_pos = token_sv.find('<', last_pos_in_token);
            }
            std::string_view sub_token_after = token_sv.substr(last_pos_in_token);
            if (!sub_token_after.empty()) {
                process_sub_token_single_pass(sub_token_after, result_line, context, local_tag_data, tag_manager, token_index, mode);
            }
        }
        current_pos = token_end;
    }
    return result_line;
}

// ===================================================================
// Core Processing Logic Function (JSON Mode)
// ===================================================================
std::string process_line_json_single_pass(
    std::string_view line_sv, GlobalTagData& local_tag_data,
    PatternRecognizer& recognizer, TagManager& tag_manager,
    ProcessingMode mode) {
    auto kvs = parse_json_kvs(line_sv);
    if (kvs.empty()) {
        return std::string(line_sv);
    }
    
    std::string result_line;
    result_line.reserve(line_sv.length());
    size_t current_pos = 0;

    for (size_t i = 0; i < kvs.size(); ++i) {
        const auto& [key, value] = kvs[i];
        size_t value_start = value.data() - line_sv.data();
        result_line.append(line_sv.substr(current_pos, value_start - current_pos));

        std::string value_as_string(value);
        if (!has_delimiter(value)) {
            std::string tag = "<" + std::string(key) + ">";
            std::string compact_id = tag_manager.get_or_create_id(tag);
            result_line.append("<").append(compact_id).append(">");
            local_tag_data[tag].push_back(value_as_string);
        } else {
            std::string value_template;
            std::string context = std::string(key);
            size_t token_index = 0;
            size_t current_val_pos = 0;
            while (current_val_pos < value_as_string.length()) {
                size_t delimiter_end = current_val_pos;
                while (delimiter_end < value_as_string.length() && is_delimiter(value_as_string[delimiter_end])) { delimiter_end++; }
                if (delimiter_end > current_val_pos) { value_template.append(value_as_string, current_val_pos, delimiter_end - current_val_pos); }
                if (delimiter_end == value_as_string.length()) break;

                size_t token_end = delimiter_end;
                while (token_end < value_as_string.length() && !is_delimiter(value_as_string[token_end])) { token_end++; }
                
                std::string_view token_sv(value_as_string.data() + delimiter_end, token_end - delimiter_end);
                process_sub_token_single_pass(token_sv, value_template, context, local_tag_data, tag_manager, token_index, mode);
                current_val_pos = token_end;
            }
            result_line.append(value_template);
        }
        current_pos = value_start + value.length();
        if (value_start > 0 && line_sv[value_start-1] == '"') {
            current_pos++;
        }
    }
    result_line.append(line_sv.substr(current_pos));
    return result_line;
}

// ===================================================================
// Multi-Pass Algorithm Analysis Stage (TEXT and JSON)
// ===================================================================
LinePiece analyze_sub_token(
    std::string_view token, std::string& context, size_t& token_index,
    std::unordered_map<std::string, int>& tag_counts) {
    if (token.empty()) {
        return { PieceType::STATIC_TEXT, "", "" };
    }
    token_index++;
    TokenType type = get_token_type(token);
    std::string full_tag;
    bool is_variable = false;

    if (type.is_pure_digit || (type.has_digit && !type.has_alpha) || (type.has_digit && type.has_alpha) || type.has_special) {
        is_variable = true;
        if (type.is_pure_digit) {
            if (token.length() <= 2) {
                full_tag = build_structured_tag("", "", std::nullopt, token.length());
            } else {
                full_tag = build_structured_tag(context, generate_regex_like_tag(token), token_index, std::nullopt);
            }
        } else if (type.has_digit && !type.has_alpha) {
            full_tag = build_structured_tag(context, generate_regex_like_tag(token), std::nullopt, std::nullopt);
        } else {
            std::string special_chars_str = extract_special_chars(token);
            full_tag = build_structured_tag(context, "_" + special_chars_str, std::nullopt, std::nullopt);
        }
    }

    if (is_variable) {
        tag_counts[full_tag]++;
        return { PieceType::VARIABLE, std::string(token), std::move(full_tag) };
    } else {
        context = token;
        return { PieceType::STATIC_TEXT, std::string(token), "" };
    }
}

AnalyzedLine analyze_line_text(
    std::string_view line_sv, PatternRecognizer& recognizer,
    std::unordered_map<std::string, int>& tag_counts) {
    AnalyzedLine result_pieces;
    size_t last_line_pos = 0;
    size_t token_index = 0;
    std::string context;

    std::vector<std::tuple<size_t, size_t, std::string>> matches;
    for (size_t i = 0; i < recognizer.compiled_patterns.size(); ++i) {
        pcre2_code* re = recognizer.compiled_patterns[i];
        PCRE2_SIZE start_offset = 0;
        pcre2_match_data* match_data = pcre2_match_data_create_from_pattern(re, nullptr);
        while(pcre2_match(re, (PCRE2_SPTR)line_sv.data(), line_sv.length(), start_offset, 0, match_data, nullptr) > 0) {
            PCRE2_SIZE* ovector = pcre2_get_ovector_pointer(match_data);
            matches.emplace_back(ovector[0], ovector[1], recognizer.substitutions[i]);
            start_offset = ovector[1];
        }
        pcre2_match_data_free(match_data);
    }
    std::sort(matches.begin(), matches.end());

    auto process_substring = [&](std::string_view sv) {
        if (sv.empty()) return;
        size_t current_pos = 0;
        while (current_pos < sv.length()) {
            size_t delimiter_end = current_pos;
            while (delimiter_end < sv.length() && is_delimiter(sv[delimiter_end])) { delimiter_end++; }
            if (delimiter_end > current_pos) {
                result_pieces.push_back({PieceType::STATIC_TEXT, std::string(sv.substr(current_pos, delimiter_end - current_pos)), ""});
            }
            if (delimiter_end == sv.length()) break;
            
            size_t token_end = delimiter_end;
            while (token_end < sv.length() && !is_delimiter(sv[token_end])) { token_end++; }
            std::string_view token_sv = sv.substr(delimiter_end, token_end - delimiter_end);
            result_pieces.push_back(analyze_sub_token(token_sv, context, token_index, tag_counts));
            current_pos = token_end;
        }
    };

    for(const auto& match : matches) {
        size_t start = std::get<0>(match);
        size_t end = std::get<1>(match);
        const std::string& tag = std::get<2>(match);
        if(start > last_line_pos) {
            process_substring(line_sv.substr(last_line_pos, start - last_line_pos));
        }
        tag_counts[tag]++;
        result_pieces.push_back({PieceType::VARIABLE, std::string(line_sv.substr(start, end - start)), tag});
        context = tag;
        last_line_pos = end;
    }

    if(last_line_pos < line_sv.length()) {
        process_substring(line_sv.substr(last_line_pos));
    }
    return result_pieces;
}

AnalyzedLine analyze_line_json(
    std::string_view line_sv, PatternRecognizer& recognizer,
    std::unordered_map<std::string, int>& tag_counts) {
    AnalyzedLine result_pieces;
    auto kvs = parse_json_kvs(line_sv);
    if (kvs.empty()) {
        result_pieces.push_back({PieceType::STATIC_TEXT, std::string(line_sv), ""});
        return result_pieces;
    }
    
    size_t last_pos = 0;
    for (const auto& [key, value] : kvs) {
        size_t value_start_ptr = value.data() - line_sv.data();
        if (value_start_ptr > last_pos) {
            result_pieces.push_back({PieceType::STATIC_TEXT, std::string(line_sv.substr(last_pos, value_start_ptr - last_pos)), ""});
        }

        if (!has_delimiter(value)) {
            std::string tag = "<" + std::string(key) + ">";
            tag_counts[tag]++;
            result_pieces.push_back({PieceType::VARIABLE, std::string(value), tag, value_start_ptr, value_start_ptr + value.length()});
        } else {
            std::string context = std::string(key);
            size_t token_index = 0;
            size_t current_val_pos = 0;
            std::string_view value_sv = value;
            while (current_val_pos < value_sv.length()) {
                size_t delimiter_end = current_val_pos;
                while (delimiter_end < value_sv.length() && is_delimiter(value_sv[delimiter_end])) { delimiter_end++; }
                if (delimiter_end > current_val_pos) {
                    result_pieces.push_back({PieceType::STATIC_TEXT, std::string(value_sv.substr(current_val_pos, delimiter_end - current_val_pos)), ""});
                }
                if (delimiter_end == value_sv.length()) break;
                
                size_t token_end = delimiter_end;
                while (token_end < value_sv.length() && !is_delimiter(value_sv[token_end])) { token_end++; }
                
                std::string_view token_sv = value_sv.substr(delimiter_end, token_end - delimiter_end);
                auto piece = analyze_sub_token(token_sv, context, token_index, tag_counts);
                piece.start_pos = value_start_ptr + delimiter_end;
                piece.end_pos = value_start_ptr + token_end;
                result_pieces.push_back(std::move(piece));
                current_val_pos = token_end;
            }
        }
        last_pos = value_start_ptr + value.length();
    }
    
    if (last_pos < line_sv.length()) {
        result_pieces.push_back({PieceType::STATIC_TEXT, std::string(line_sv.substr(last_pos)), ""});
    }
    return result_pieces;
}

// ===================================================================
// STAGE 2: Post-Processing and Parallel Core (Generic and Optimized Versions)
// ===================================================================
void process_aggregated_tags(
    const GlobalTagData& tag_data, const std::unordered_map<std::string, std::string>& tag_to_id_map,
    const std::string& processed_dir, ProcessingMode mode) {

    // --- FAST Mode: File Aggregation Optimization (Unchanged) ---
    if (mode == ProcessingMode::FAST) {
        std::cout << "  (Fast mode: aggregating variable data into single files...)\n";
        std::ofstream all_mappings_file(std::filesystem::path(processed_dir) / "all_mappings.fast.txt", std::ios::binary);
        std::ofstream all_ids_file(std::filesystem::path(processed_dir) / "all_ids.fast.bin", std::ios::binary);
        std::ofstream index_file(std::filesystem::path(processed_dir) / "index.fast.csv");
        
        if (!all_mappings_file || !all_ids_file || !index_file) {
            throw std::runtime_error("Could not create aggregation files for Fast mode.");
        }
        
        index_file << "tag_id,mapping_offset,mapping_size,ids_offset,ids_size\n";

        for (const auto& pair : tag_data) {
            const std::string& tag_name = pair.first;
            const std::vector<std::string>& values = pair.second;
            if (values.empty()) continue;

            auto it = tag_to_id_map.find(tag_name);
            if (it == tag_to_id_map.end()) {
                std::cerr << "Critical Error: Could not find an ID for the tag '" << tag_name << "'. Skipping." << std::endl;
                continue;
            }
            const std::string& tag_id = it->second;

            StoredContent content = generate_stored_content(values);

            size_t mapping_offset = all_mappings_file.tellp();
            size_t ids_offset = all_ids_file.tellp();

            all_mappings_file.write(content.mapping_data.c_str(), content.mapping_data.size());
            all_ids_file.write(reinterpret_cast<const char*>(content.ids_data.data()), content.ids_data.size());

            index_file << tag_id << ","
                       << mapping_offset << "," << content.mapping_data.size() << ","
                       << ids_offset << "," << content.ids_data.size() << "\n";
        }
        return;
    }

    // *** MODIFICATION START: Modify Normal mode to use file aggregation ***
    // --- NORMAL Mode: Now also uses file aggregation ---
    std::cout << "  (Normal mode: aggregating variable data into single files...)\n";
    std::ofstream all_mappings_file(std::filesystem::path(processed_dir) / "all_mappings.normal.txt", std::ios::binary);
    std::ofstream all_variables_file(std::filesystem::path(processed_dir) / "all_variables.normal.bin", std::ios::binary);
    std::ofstream index_file(std::filesystem::path(processed_dir) / "index.normal.csv");
    
    if (!all_mappings_file || !all_variables_file || !index_file) {
        throw std::runtime_error("Could not create aggregation files for Normal mode.");
    }
    
    // Index file header, file1 points to all_variables.normal.bin, file2 points to all_mappings.normal.txt
    index_file << "tag_id,storage_type,file1_offset,file1_size,file2_offset,file2_size\n";

    const size_t MAX_SAFE_LLONG_STR_LEN = 18;
    for (const auto& pair : tag_data) {
        const std::string& tag_name = pair.first;
        const std::vector<std::string>& values = pair.second;
        if (values.empty()) continue;

        auto it = tag_to_id_map.find(tag_name);
        if (it == tag_to_id_map.end()) {
            std::cerr << "Critical Error: Could not find an ID for the tag '" << tag_name << "'. Skipping." << std::endl;
            continue;
        }
        const std::string& tag_id = it->second;

        auto record_to_index = [&](const std::string& storage_type, size_t f1_off, size_t f1_size, size_t f2_off, size_t f2_size) {
            index_file << tag_id << "," << storage_type << "," << f1_off << "," << f1_size << "," << f2_off << "," << f2_size << "\n";
        };

        auto dictionary_encode_and_store = [&]() {
            StoredContent content = generate_stored_content(values);
            size_t var_offset = all_variables_file.tellp();
            size_t map_offset = all_mappings_file.tellp();
            all_variables_file.write(reinterpret_cast<const char*>(content.ids_data.data()), content.ids_data.size());
            all_mappings_file.write(content.mapping_data.c_str(), content.mapping_data.size());
            record_to_index("MAPPING", var_offset, content.ids_data.size(), map_offset, content.mapping_data.size());
        };

        // Generic handling for dictionary encoding
        if (tag_name.find("<B>") != std::string::npos || tag_name.find("<M>") != std::string::npos || tag_name.find("<K>") != std::string::npos || tag_name.find("<G>") != std::string::npos || tag_name.find("<X>") != std::string::npos || tag_name.find("<Y>") != std::string::npos || tag_name.find("<Z>") != std::string::npos) {
            dictionary_encode_and_store();
            continue;
        }
        
        bool all_same = std::all_of(values.begin() + 1, values.end(), [&](const auto& s){ return s == values[0]; });
        if (all_same) {
            bool is_numeric = !values[0].empty() && std::all_of(values[0].begin(), values[0].end(), ::isdigit);
            if (is_numeric && values[0].length() <= MAX_SAFE_LLONG_STR_LEN) {
                auto encoded = elastic_encode(std::stoll(values[0]));
                size_t var_offset = all_variables_file.tellp();
                all_variables_file.write(reinterpret_cast<const char*>(encoded.data()), encoded.size());
                record_to_index("ALLSAME", var_offset, encoded.size(), 0, 0);
            } else {
                dictionary_encode_and_store();
            }
            continue;
        }
        
        bool all_values_are_numeric = true;
        for (const auto& s : values) {
            if (s.empty() || !std::all_of(s.begin(), s.end(), ::isdigit)) {
                all_values_are_numeric = false;
                break;
            }
        }
        
        if (!all_values_are_numeric) {
            dictionary_encode_and_store();
            continue;
        }

        bool safe_to_convert = true;
        bool is_predefined_numeric = (tag_name.find("<T>") != std::string::npos || tag_name.find("<I>") != std::string::npos || tag_name.find("<D>") != std::string::npos);
        for (const auto& s : values) {
            if (s.length() > MAX_SAFE_LLONG_STR_LEN) {
                safe_to_convert = false;
                break;
            }
        }
        
        if (safe_to_convert) {
            std::list<int64_t> numbers;
            for (const auto& val : values) {
                if(!val.empty()) numbers.push_back(std::stoll(val));
            }
            
            bool should_transform = (tag_name.find("<I>") == std::string::npos);
            if (is_predefined_numeric && should_transform) {
                if (numbers.size() > 100) {
                    std::set<size_t> lengths;
                    size_t count = 0;
                    for (int64_t num : numbers) {
                        lengths.insert(std::to_string(num).length());
                        if (++count >= 100 || lengths.size() >= 3) break;
                    }
                    if (lengths.size() >= 3) {
                        should_transform = false;
                    }
                }
            }

            std::list<int64_t> to_write = should_transform ? delta_transform(numbers) : numbers;
            size_t var_offset = all_variables_file.tellp();
            for (int64_t num : to_write) {
                auto encoded = elastic_encode(num);
                all_variables_file.write(reinterpret_cast<const char*>(encoded.data()), encoded.size());
            }
            size_t var_size = static_cast<size_t>(all_variables_file.tellp()) - var_offset;
            record_to_index(should_transform ? "NUMERIC_DELTA" : "NUMERIC_NODELTA", var_offset, var_size, 0, 0);
        } else {
            dictionary_encode_and_store();
        }
    }
    // *** MODIFICATION END ***
}



uintmax_t process_and_compress_chunk(
    const std::vector<std::string>& block, const std::string& logname, LogMode log_mode,
    const std::string& base_output_dir, int chunk_id, CompressionKernel kernel,
    ProcessingMode mode, bool keep_temp_files) {
    std::cout << "Processing chunk " << chunk_id << " with " << block.size() << " lines (single-pass)..." << std::endl;
    PatternRecognizer recognizer(logname);
    TagManager tag_manager;

    std::vector<std::string> modified_lines;
    modified_lines.reserve(block.size());
    GlobalTagData chunk_tag_data;

    for (const auto& line : block) {
        if (line.empty()) {
            modified_lines.push_back("");
            continue;
        }
        GlobalTagData line_tag_data;
        if (log_mode == LogMode::TEXT) {
            modified_lines.push_back(process_line_text_single_pass(line, line_tag_data, recognizer, tag_manager, mode));
        } else { // LogMode::JSON
            modified_lines.push_back(process_line_json_single_pass(line, line_tag_data, recognizer, tag_manager, mode));
        }

        for (auto& pair : line_tag_data) {
            std::vector<std::string>& target_vec = chunk_tag_data[pair.first];
            target_vec.insert(
                target_vec.end(),
                std::make_move_iterator(pair.second.begin()),
                std::make_move_iterator(pair.second.end())
            );
        }
    }

    const std::string chunk_output_dir = std::filesystem::path(base_output_dir) / ("chunk_" + std::to_string(chunk_id));
    const std::string processed_tags_dir = std::filesystem::path(chunk_output_dir) / "processed_tags";
    ensure_directory_exists(processed_tags_dir);
    
    store_content_with_ids(modified_lines, std::filesystem::path(chunk_output_dir) / (logname + "_templates.mapping.txt"), std::filesystem::path(chunk_output_dir) / (logname + "_templates.ids.bin"));
    
    process_aggregated_tags(chunk_tag_data, tag_manager.get_tag_to_id_map(), processed_tags_dir, mode);
    
    tag_manager.save_mapping(std::filesystem::path(chunk_output_dir) / "tags_mapping.txt");

    CompressionInfo comp_info = get_compression_info(kernel);
    std::string archive_path = std::filesystem::path(base_output_dir) / ("chunk_" + std::to_string(chunk_id) + comp_info.extension);
    std::string command = "tar " + comp_info.tar_flag + " " + archive_path + " -C " + chunk_output_dir + " .";
    int ret = std::system(command.c_str());

    if (ret != 0) {
        std::cerr << "Warning: tar command failed for chunk " << chunk_id << "." << std::endl;
        if (!keep_temp_files) { std::filesystem::remove_all(chunk_output_dir); }
        return 0;
    }

    uintmax_t compressed_size = 0;
    try {
        if (std::filesystem::exists(archive_path)) {
            compressed_size = std::filesystem::file_size(archive_path);
        }
    } catch(const std::filesystem::filesystem_error& e) {
        std::cerr << "Error getting file size for " << archive_path << ": " << e.what() << std::endl;
    }

    if (!keep_temp_files) {
        std::filesystem::remove_all(chunk_output_dir);
    }
    std::cout << "Finished chunk " << chunk_id << ". Compressed size: " << compressed_size / 1024 << " KB." << std::endl;
    return compressed_size;
}

uintmax_t process_and_compress_chunk_with_threshold(
    const std::vector<std::string>& block, const std::string& logname, LogMode log_mode,
    const std::string& base_output_dir, int chunk_id, size_t frequency_threshold,
    CompressionKernel kernel, ProcessingMode mode, bool keep_temp_files) {
    std::cout << "Processing chunk " << chunk_id << " with threshold " << frequency_threshold << " (" << block.size() << " lines)..." << std::endl;
    PatternRecognizer recognizer(logname);
    TagManager tag_manager;

    std::unordered_map<std::string, int> tag_counts;
    for (const auto& line : block) {
        if (line.empty()) continue;
        if (log_mode == LogMode::TEXT) {
            analyze_line_text(line, recognizer, tag_counts);
        } else {
            analyze_line_json(line, recognizer, tag_counts);
        }
    }

    const std::unordered_set<std::string> predefined_tags(recognizer.substitutions.begin(), recognizer.substitutions.end());
    std::unordered_set<std::string> frequent_tags;
    for(const auto& pair : tag_counts) {
        if (predefined_tags.count(pair.first) > 0 || (size_t)pair.second > frequency_threshold) {
            frequent_tags.insert(pair.first);
        }
    }

    std::vector<std::string> final_modified_lines;
    final_modified_lines.reserve(block.size());
    GlobalTagData final_tag_data;

    for (const auto& line : block) {
        if (line.empty()) {
            final_modified_lines.push_back("");
            continue;
        };
        std::unordered_map<std::string, int> dummy_tag_counts;
        AnalyzedLine analyzed_line;
        if (log_mode == LogMode::TEXT) {
            analyzed_line = analyze_line_text(line, recognizer, dummy_tag_counts);
        } else {
            analyzed_line = analyze_line_json(line, recognizer, dummy_tag_counts);
        }
        
        std::string result_line;
        result_line.reserve(line.length());
        for (const auto& piece : analyzed_line) {
            if (piece.type == PieceType::STATIC_TEXT) {
                result_line.append(piece.original_token);
            } else {
                if (frequent_tags.count(piece.full_tag) > 0) {
                    std::string compact_id = tag_manager.get_or_create_id(piece.full_tag);
                    result_line.append("<").append(compact_id).append(">");
                    
                    std::string value_to_store;
                    const std::string& tag = piece.full_tag;
                    const std::string& token = piece.original_token;

                    if (mode == ProcessingMode::FAST) {
                        value_to_store = token;
                    } else {
                        if (tag == "<I>") {
                            std::string ip_part;
                            for (char c : token) {
                                if (c == '.') { while (ip_part.length() < 3) ip_part.insert(0, 1, '0'); value_to_store += ip_part; ip_part.clear(); } 
                                else if (isdigit(c)) { ip_part += c; }
                            }
                            while (ip_part.length() < 3) ip_part.insert(0, 1, '0');
                            value_to_store += ip_part;
                        } else if (tag == "<X>" || tag == "<Y>" || tag == "<Z>" || tag == "<B>" || tag == "<M>" || tag == "<K>" || tag == "<G>" || tag == "<S>" || tag == "<E>" || tag == "<F>" || tag == "<A>") {
                            value_to_store = token;
                        } else {
                            TokenType type = get_token_type(token);
                            if (type.has_digit && !type.has_alpha) {
                                value_to_store = extract_digits(token);
                            } else {
                                value_to_store = token;
                            }
                        }
                    }
                    final_tag_data[tag].push_back(std::move(value_to_store));
                } else {
                    result_line.append(piece.original_token);
                }
            }
        }
        final_modified_lines.push_back(std::move(result_line));
    }
    
    const std::string chunk_output_dir = std::filesystem::path(base_output_dir) / ("chunk_" + std::to_string(chunk_id));
    const std::string processed_tags_dir = std::filesystem::path(chunk_output_dir) / "processed_tags";
    ensure_directory_exists(processed_tags_dir);
    
    store_content_with_ids(final_modified_lines, std::filesystem::path(chunk_output_dir) / (logname + "_templates.mapping.txt"), std::filesystem::path(chunk_output_dir) / (logname + "_templates.ids.bin"));

    process_aggregated_tags(final_tag_data, tag_manager.get_tag_to_id_map(), processed_tags_dir, mode);
    
    tag_manager.save_mapping(std::filesystem::path(chunk_output_dir) / "tags_mapping.txt");

    CompressionInfo comp_info = get_compression_info(kernel);
    std::string archive_path = std::filesystem::path(base_output_dir) / ("chunk_" + std::to_string(chunk_id) + comp_info.extension);
    std::string command = "tar " + comp_info.tar_flag + " " + archive_path + " -C " + chunk_output_dir + " .";
    int ret = std::system(command.c_str());

    if (ret != 0) {
        std::cerr << "Warning: tar command failed for chunk " << chunk_id << "." << std::endl;
        if (!keep_temp_files) { std::filesystem::remove_all(chunk_output_dir); }
        return 0;
    }
    
    uintmax_t compressed_size = 0;
    try {
        if (std::filesystem::exists(archive_path)) {
            compressed_size = std::filesystem::file_size(archive_path);
        }
    } catch(const std::filesystem::filesystem_error& e) {
        std::cerr << "Error getting file size for " << archive_path << ": " << e.what() << std::endl;
    }

    if (!keep_temp_files) {
        std::filesystem::remove_all(chunk_output_dir);
    }
    std::cout << "Finished chunk " << chunk_id << ". Compressed size: " << compressed_size / 1024 << " KB." << std::endl;
    return compressed_size;
}

// ===================================================================
// Experiment Result Logging
// ===================================================================
// Use a static mutex to ensure thread-safe file writing, even across multiple program instances launched by a script.
static std::mutex result_file_mutex;

void log_experiment_result(
    const std::string& logname,
    const std::string& log_mode,
    const std::string& processing_mode,
    size_t block_size,
    int num_threads,
    size_t frequency_threshold,
    const std::string& compression_kernel,
    long long execution_time_ms,
    double original_size_mb,
    double final_size_mb,
    double ratio,
    double compression_speed_mbps) // New parameter
{
    std::lock_guard<std::mutex> lock(result_file_mutex);

    const std::string result_filename = "experiment_results.csv";
    bool file_exists = std::filesystem::exists(result_filename);

    std::ofstream result_file(result_filename, std::ios::app);
    if (!result_file.is_open()) {
        std::cerr << "Error: Could not open experiment results file: " << result_filename << std::endl;
        return;
    }

    // If the file is newly created (or empty), write the header
    if (!file_exists || result_file.tellp() == 0) {
        result_file << "Timestamp,"
                    << "LogName,"
                    << "LogMode,"
                    << "ProcessingMode,"
                    << "BlockSize,"
                    << "NumThreads,"
                    << "FreqThreshold,"
                    << "CompressionKernel,"
                    << "ExecutionTime_ms,"
                    << "OriginalSize_MB,"
                    << "FinalSize_MB,"
                    << "Ratio,"
                    << "CompressionSpeed_MBps\n"; // New header
    }

    // Get the current timestamp
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    // std::localtime is not thread-safe in a multi-threaded environment, but it's safe here because the entire function is protected by a mutex.
    ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %X");

    result_file << ss.str() << ","
                << logname << ","
                << log_mode << ","
                << processing_mode << ","
                << block_size << ","
                << num_threads << ","
                << frequency_threshold << ","
                << compression_kernel << ","
                << execution_time_ms << ","
                << std::fixed << std::setprecision(4) << original_size_mb << ","
                << std::fixed << std::setprecision(4) << final_size_mb << ","
                << std::fixed << std::setprecision(3) << ratio << "," // Keep three decimal places for Ratio
                << std::fixed << std::setprecision(2) << compression_speed_mbps << "\n"; // New speed, keep two decimal places
    
    std::cout << "Experiment results have been logged to " << result_filename << std::endl;
}


// ===================================================================
// Main Function
// ===================================================================
int main(int argc, char* argv[]) {
    if (argc < 8) {
        std::cerr << "Usage: " << argv[0] << " <logname> <log_mode> <block_size> <num_threads> <frequency_threshold> <compression_kernel> <processing_mode> [--keep-temp-files]" << std::endl;
        return 1;
    }

    const std::string logname = argv[1];
    std::string log_mode_str = argv[2];
    size_t BLOCK_SIZE = std::stoul(argv[3]);
    int num_threads = std::stoi(argv[4]);
    size_t frequency_threshold = std::stoul(argv[5]);
    std::string kernel_str = argv[6];
    std::string processing_mode_str = argv[7];
    bool keep_temp_files = (argc > 8 && std::string(argv[8]) == "--keep-temp-files");

    auto log_mode_opt = parse_log_mode(log_mode_str);
    if (!log_mode_opt) { std::cerr << "Invalid log mode: " << log_mode_str << std::endl; return 1; }
    LogMode log_mode = *log_mode_opt;

    auto processing_mode_opt = parse_processing_mode(processing_mode_str);
    if (!processing_mode_opt) { std::cerr << "Invalid processing mode: " << processing_mode_str << std::endl; return 1; }
    ProcessingMode processing_mode = *processing_mode_opt;

    auto kernel_opt = parse_kernel_from_string(kernel_str);
    if (!kernel_opt) { std::cerr << "Invalid compression kernel: " << kernel_str << std::endl; return 1; }
    CompressionKernel kernel = *kernel_opt;

    std::cout << "\nLog Name: " << logname << ", Mode: " << log_mode_str << ", Processing: " << processing_mode_str << std::endl;
    
    const std::string log_path_str = "Logs/" + logname + "/" + logname + ".log";
    const std::filesystem::path log_path(log_path_str);
    const std::string base_output_dir = "output/" + logname;

    if (!std::filesystem::exists(log_path)) { std::cerr << "Error: Log file not found: " << log_path_str << std::endl; return 1; }
    if (std::filesystem::exists(base_output_dir)) std::filesystem::remove_all(base_output_dir);
    ensure_directory_exists(base_output_dir);

    auto start_time = std::chrono::high_resolution_clock::now();
    uintmax_t total_compressed_size = 0;
    
    std::ifstream log_file(log_path);
    if (!log_file.is_open()) { std::cerr << "Error: Failed to open log file: " << log_path_str << std::endl; return 1; }
    
    std::vector<std::future<uintmax_t>> futures;
    int chunk_id = 0;

    while (log_file) {
        std::vector<std::string> block;
        block.reserve(BLOCK_SIZE);
        std::string line;
        for (size_t i = 0; i < BLOCK_SIZE && std::getline(log_file, line); ++i) {
            block.push_back(std::move(line));
        }

        if (!block.empty()) {
            if (frequency_threshold > 0) {
                futures.push_back(std::async(std::launch::async, process_and_compress_chunk_with_threshold, std::move(block), logname, log_mode, base_output_dir, chunk_id++, frequency_threshold, kernel, processing_mode, keep_temp_files));
            } else {
                futures.push_back(std::async(std::launch::async, process_and_compress_chunk, std::move(block), logname, log_mode, base_output_dir, chunk_id++, kernel, processing_mode, keep_temp_files));
            }
        }
        
        if (futures.size() >= static_cast<size_t>(num_threads)) {
            for (auto& fut : futures) {
                total_compressed_size += fut.get();
            }
            futures.clear();
        }
    }
    log_file.close();

    for (auto& fut : futures) {
        total_compressed_size += fut.get();
    }
    std::cout << "All chunks processed." << std::endl;

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    std::cout << "\n--- Execution Summary ---" << std::endl;
    try {
        uintmax_t original_size = std::filesystem::file_size(log_path);
        double original_size_mb = original_size / (1024.0 * 1024.0);
        double final_size_mb = total_compressed_size / (1024.0 * 1024.0);
        double ratio = 0.0;
        double compression_speed_mbps = 0.0;

        // Calculate compression speed
        double execution_time_s = duration.count() / 1000.0;
        if (execution_time_s > 0) {
            compression_speed_mbps = original_size_mb / execution_time_s;
        }
        
        if (total_compressed_size > 0 || kernel == CompressionKernel::NONE) {
            ratio = (total_compressed_size > 0) ? static_cast<double>(original_size) / total_compressed_size : 0.0;
            std::cout << "Total execution time: " << duration.count() << " ms\n"
                      << "Original log size: " << std::fixed << std::setprecision(2) << original_size_mb << " MB\n"
                      << "Total final size: " << std::fixed << std::setprecision(2) << final_size_mb << " MB\n"
                      << "Effective Ratio: " << std::fixed << std::setprecision(2) << ratio << ":1\n"
                      << "Compression Speed: " << std::fixed << std::setprecision(2) << compression_speed_mbps << " MB/s\n"; // Display speed
        } else {
            std::cerr << "Compression failed or produced zero-sized output.\n";
            std::cout << "Total execution time: " << duration.count() << " ms\n";
        }
        
        // Call the function to log experiment results and pass the speed
        log_experiment_result(
            logname,
            log_mode_str,
            processing_mode_str,
            BLOCK_SIZE,
            num_threads,
            frequency_threshold,
            kernel_str,
            duration.count(),
            original_size_mb,
            final_size_mb,
            ratio,
            compression_speed_mbps // Pass the newly calculated speed
        );

    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Error getting file sizes for final report: " << e.what() << std::endl;
        // Even if there's an error getting the file size, log the available information
        log_experiment_result(
            logname,
            log_mode_str,
            processing_mode_str,
            BLOCK_SIZE,
            num_threads,
            frequency_threshold,
            kernel_str,
            duration.count(),
            -1.0, // Use -1 to indicate a failure to get the value
            -1.0,
            -1.0, // Use -1 to indicate a failure to get the value
            -1.0  // Use -1 to indicate a failure to get the value
        );
    }
    
    return 0;
}