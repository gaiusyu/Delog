#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <algorithm>
#include <stdexcept>
#include <list>
#include <iomanip>
#include <cstdlib>
#include <chrono>
#include <future>
#include <sstream>
#include <thread>
#include <mutex>
#include <numeric>
#include <regex>

#include <archive.h>
#include <archive_entry.h>

// ===================================================================
// Part 1: Helper Functions (No changes)
// ===================================================================
int64_t zigzag_decode(uint64_t n) { return (n >> 1) ^ -(n & 1); }
int64_t elastic_decode(const std::vector<unsigned char>& num_bytes) {
    uint64_t ret = 0; int offset = 0;
    for (auto cur : num_bytes) {
        ret |= (static_cast<uint64_t>(cur & 0x7F) << offset);
        if ((cur & 0x80) == 0) break;
        offset += 7;
    }
    return zigzag_decode(ret);
}
std::list<int64_t> delta_transform_inverse(const std::list<int64_t>& l) {
    if (l.empty()) return {};
    std::list<int64_t> new_list; auto it = l.begin();
    int64_t last = *it; new_list.push_back(last);
    for (++it; it != l.end(); ++it) {
        last = last + *it; new_list.push_back(last);
    }
    return new_list;
}
void extract_archive(const std::filesystem::path& archive_path, const std::filesystem::path& dest_dir) {
    struct archive *a = archive_read_new(); struct archive *ext = archive_write_disk_new();
    archive_read_support_format_all(a); archive_read_support_filter_all(a);
    archive_write_disk_set_options(ext, ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_ACL | ARCHIVE_EXTRACT_FFLAGS);
    archive_write_disk_set_standard_lookup(ext);
    if (archive_read_open_filename(a, archive_path.c_str(), 10240) != ARCHIVE_OK) { throw std::runtime_error("Cannot open archive: " + std::string(archive_error_string(a))); }
    for (;;) {
        struct archive_entry *entry; int r = archive_read_next_header(a, &entry);
        if (r == ARCHIVE_EOF) break; if (r < ARCHIVE_OK) throw std::runtime_error("Header read error: " + std::string(archive_error_string(a)));
        const std::filesystem::path new_path = dest_dir / archive_entry_pathname(entry); archive_entry_set_pathname(entry, new_path.c_str());
        if (archive_write_header(ext, entry) < ARCHIVE_OK) throw std::runtime_error("Header write error: " + std::string(archive_error_string(ext)));
        if (archive_entry_size(entry) > 0) {
            const void *buff; size_t size; int64_t offset;
            for (;;) {
                r = archive_read_data_block(a, &buff, &size, &offset); if (r == ARCHIVE_EOF) break;
                if (r < ARCHIVE_OK) throw std::runtime_error("Data read error: " + std::string(archive_error_string(a)));
                if (archive_write_data_block(ext, buff, size, offset) < ARCHIVE_OK) throw std::runtime_error("Data write error: " + std::string(archive_error_string(ext)));
            }
        }
        if (archive_write_finish_entry(ext) < ARCHIVE_OK) throw std::runtime_error("Finish entry error: " + std::string(archive_error_string(ext)));
    }
    archive_read_close(a); archive_read_free(a); archive_write_close(ext); archive_write_free(ext);
}

// ===================================================================
// Part 2: True Streaming Data Providers
// ===================================================================
class IDataProvider { public: virtual ~IDataProvider() = default; virtual std::string get_next() = 0; };

// Reads one elastic-encoded number from a stream. Returns false if EOF.
bool read_one_elastic_num(std::ifstream& fs, int64_t& out_num) {
    std::vector<unsigned char> num_byte_vec;
    char byte_char;
    while (fs.get(byte_char)) {
        unsigned char current_byte = static_cast<unsigned char>(byte_char);
        num_byte_vec.push_back(current_byte);
        if (current_byte < 128) {
            out_num = elastic_decode(num_byte_vec);
            return true;
        }
    }
    return false; // Reached end of file
}

class StreamingNumberProvider : public IDataProvider {
    std::ifstream fs_;
    bool apply_delta_inverse_;
    bool is_first_ = true;
    int64_t last_value_ = 0;
public:
    StreamingNumberProvider(const std::filesystem::path& bin_file, bool apply_delta_inverse) 
        : apply_delta_inverse_(apply_delta_inverse) {
        fs_.open(bin_file, std::ios::binary);
        if (!fs_) throw std::runtime_error("Cannot open bin file for streaming: " + bin_file.string());
    }
    std::string get_next() override {
        int64_t decoded_num;
        if (!read_one_elastic_num(fs_, decoded_num)) {
            throw std::out_of_range("StreamingNumberProvider exhausted.");
        }
        if (apply_delta_inverse_) {
            if (is_first_) {
                last_value_ = decoded_num;
                is_first_ = false;
            } else {
                last_value_ = last_value_ + decoded_num;
            }
            return std::to_string(last_value_);
        } else {
            return std::to_string(decoded_num);
        }
    }
};

class StreamingMappingProvider : public IDataProvider {
    std::vector<std::string> mapping_; // The dictionary is kept in memory, as it's usually smaller.
    std::ifstream ids_fs_;
public:
    StreamingMappingProvider(const std::filesystem::path& mapping_file, const std::filesystem::path& ids_file) {
        std::ifstream map_fs(mapping_file);
        if (!map_fs) throw std::runtime_error("Cannot open mapping file: " + mapping_file.string());
        std::string line;
        while (std::getline(map_fs, line)) { mapping_.push_back(line); }
        
        ids_fs_.open(ids_file, std::ios::binary);
        if (!ids_fs_) throw std::runtime_error("Cannot open ids file for streaming: " + ids_file.string());
    }
    std::string get_next() override {
        int64_t id;
        if (!read_one_elastic_num(ids_fs_, id)) {
            throw std::out_of_range("StreamingMappingProvider exhausted.");
        }
        if (id < 1 || static_cast<size_t>(id) > mapping_.size()) {
            throw std::runtime_error("Invalid ID " + std::to_string(id) + " from stream.");
        }
        return mapping_[id - 1];
    }
};

class Decompressor {
private:
    std::unique_ptr<IDataProvider> template_provider_;
    std::unordered_map<std::string, std::unique_ptr<IDataProvider>> data_providers_;
    std::string reconstruct_value(const std::string& tag, const std::string& value) { (void)tag; return value; }
public:
    Decompressor(const std::filesystem::path& archive_path, const std::filesystem::path& temp_base_path) {
        auto temp_dir = temp_base_path / archive_path.stem();
        if (std::filesystem::exists(temp_dir)) std::filesystem::remove_all(temp_dir);
        std::filesystem::create_directories(temp_dir);
        extract_archive(archive_path, temp_dir);

        std::filesystem::path all_map, all_ids, var_map, var_ids;
        for (const auto& entry : std::filesystem::directory_iterator(temp_dir)) {
            std::string filename = entry.path().filename().string();
            if (filename.find("allmapping.txt") != std::string::npos) all_map = entry.path();
            else if (filename.find("allids.bin") != std::string::npos) all_ids = entry.path();
            else if (filename.find("variablesetmapping.txt") != std::string::npos) var_map = entry.path();
            else if (filename.find("variablesetids.bin") != std::string::npos) var_ids = entry.path();
        }

        if (!all_map.empty() && !all_ids.empty()) {
            template_provider_ = std::make_unique<StreamingMappingProvider>(all_map, all_ids);
        } else { throw std::runtime_error("Template mapping/ids files not found in " + temp_dir.string()); }
        
        if (!var_map.empty() && !var_ids.empty()) {
            data_providers_["<*>"] = std::make_unique<StreamingMappingProvider>(var_map, var_ids);
        } else { std::cerr << "Warning: VariableSet files not found in " << temp_dir.string() << "." << std::endl; }
        
        std::unordered_set<std::string> non_delta_labels = {"I", "a", "b", "c"};
        for (const auto& entry : std::filesystem::directory_iterator(temp_dir)) {
            std::string filename = entry.path().filename().string();
            std::regex tag_pattern("_(.+)_\\.bin");
            std::smatch match;
            if (std::regex_match(filename, match, tag_pattern) && match.size() == 2) {
                std::string tag_content = match[1].str();
                std::string original_tag = "<" + tag_content + ">";
                bool apply_delta_inverse = (non_delta_labels.find(tag_content) == non_delta_labels.end());
                data_providers_[original_tag] = std::make_unique<StreamingNumberProvider>(entry.path(), apply_delta_inverse);
            }
        }
        std::filesystem::remove_all(temp_dir);
    }
    void decompress_to_stream(std::ostream& out) {
        if (!template_provider_) throw std::runtime_error("Template provider not initialized.");
        try {
            while(true) {
                std::string current_template = template_provider_->get_next();
                size_t last_pos = 0;
                while(true) {
                    size_t placeholder_pos = current_template.find('<', last_pos);
                    if (placeholder_pos == std::string::npos) { out << current_template.substr(last_pos) << "\n"; break; }
                    out << current_template.substr(last_pos, placeholder_pos - last_pos);
                    size_t placeholder_end = current_template.find('>', placeholder_pos);
                    if (placeholder_end == std::string::npos) { out << current_template.substr(placeholder_pos) << "\n"; break; }
                    std::string tag = current_template.substr(placeholder_pos, placeholder_end - placeholder_pos + 1);
                    auto it = data_providers_.find(tag);
                    if (it != data_providers_.end()) {
                        try { out << reconstruct_value(tag, it->second->get_next()); } 
                        catch (const std::out_of_range&) { out << "[EXHAUSTED_PROVIDER_FOR_" + tag + "]"; }
                    } else { out << "[MISSING_PROVIDER_FOR_" + tag + "]"; }
                    last_pos = placeholder_end + 1;
                }
            }
        } catch(const std::out_of_range&) {}
    }
};

// ===================================================================
// Part 3: Main Function (No changes needed here, as it's already streaming output)
// ===================================================================
std::filesystem::path decompress_chunk_to_file(int chunk_id, const std::filesystem::path& chunk_path, const std::filesystem::path& temp_base_path) {
    const std::filesystem::path temp_output_path = temp_base_path / (std::to_string(chunk_id) + ".tmp");
    try {
        std::ofstream temp_out_stream(temp_output_path, std::ios::binary);
        if (!temp_out_stream) throw std::runtime_error("Failed to open temporary file for writing: " + temp_output_path.string());
        Decompressor decompressor(chunk_path, temp_base_path);
        decompressor.decompress_to_stream(temp_out_stream);
    } catch (const std::exception& e) {
        std::cerr << "Error processing chunk " << chunk_path.string() << ": " << e.what() << std::endl;
        std::ofstream error_stream(temp_output_path);
        error_stream << "[ERROR: Failed to process chunk " << chunk_path.filename().string() << ": " << e.what() << "]\n";
    }
    return temp_output_path;
}

static std::mutex result_file_mutex;
void log_decompression_result(const std::string& logname, int num_threads, double compressed_size_mb, double decompressed_size_mb, long long execution_time_ms, double decompression_speed_mbps) {
    std::lock_guard<std::mutex> lock(result_file_mutex);
    const std::string result_filename = "decompression_results.csv";
    bool file_exists = std::filesystem::exists(result_filename);
    std::ofstream result_file(result_filename, std::ios::app);
    if (!result_file.is_open()) { std::cerr << "Error: Could not open experiment results file: " << result_filename << std::endl; return; }
    if (!file_exists || result_file.tellp() == 0) { result_file << "Timestamp,LogName,NumThreads,CompressedSize_MB,DecompressedSize_MB,ExecutionTime_ms,DecompressionSpeed_MBps\n"; }
    auto now = std::chrono::system_clock::now(); auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    #ifdef _MSC_VER
        struct tm tm_buf; localtime_s(&tm_buf, &in_time_t); ss << std::put_time(&tm_buf, "%Y-%m-%d %X");
    #else
        ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %X");
    #endif
    result_file << ss.str() << "," << logname << "," << num_threads << ","
                << std::fixed << std::setprecision(4) << compressed_size_mb << ","
                << std::fixed << std::setprecision(4) << decompressed_size_mb << ","
                << execution_time_ms << "," << std::fixed << std::setprecision(2) << decompression_speed_mbps << "\n";
    std::cout << "Decompression experiment results logged to " << result_filename << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <compressed_dir> <output_log_file> <num_threads>\n";
        return 1;
    }
    const std::filesystem::path compressed_dir(argv[1]);
    const std::filesystem::path output_file(argv[2]);
    int num_threads = std::stoi(argv[3]);
    const std::filesystem::path temp_base_path = "denum_decompress_temp";

    if (!std::filesystem::is_directory(compressed_dir)) {
        std::cerr << "Error: Not a directory: " << compressed_dir << std::endl;
        return 1;
    }
    if (std::filesystem::exists(temp_base_path)) std::filesystem::remove_all(temp_base_path);
    std::filesystem::create_directories(temp_base_path);
    if (std::filesystem::exists(output_file)) std::filesystem::remove(output_file);

    try {
        std::vector<std::filesystem::path> chunk_files;
        for (const auto& entry : std::filesystem::directory_iterator(compressed_dir)) {
            if (entry.is_regular_file() && entry.path().filename().string().rfind("compressed", 0) == 0) {
                chunk_files.push_back(entry.path());
            }
        }
        std::sort(chunk_files.begin(), chunk_files.end(), [](const auto& a, const auto& b) {
            try {
                auto get_id = [](const std::string& s) {
                    size_t start = s.find_first_of("0123456789"); if (start == std::string::npos) return -1;
                    size_t end = s.find_first_not_of("0123456789", start); return std::stoi(s.substr(start, end - start));
                };
                return get_id(a.filename().string()) < get_id(b.filename().string());
            } catch (...) { return a.string() < b.string(); }
        });

        auto start_time = std::chrono::high_resolution_clock::now();
        std::cout << "Found " << chunk_files.size() << " chunks. Decompressing with " << num_threads << " threads using a true streaming approach..." << std::endl;

        std::ofstream out_log(output_file, std::ios::binary);
        if (!out_log) throw std::runtime_error("Cannot open output file: " + output_file.string());
        
        std::vector<std::future<std::filesystem::path>> futures;
        for (size_t i = 0; i < chunk_files.size(); ++i) {
            futures.push_back(std::async(std::launch::async, decompress_chunk_to_file, i, chunk_files[i], temp_base_path));
        }

        for (size_t i = 0; i < futures.size(); ++i) {
            std::filesystem::path temp_file_path = futures[i].get();
            if (std::filesystem::exists(temp_file_path)) {
                std::ifstream temp_in_stream(temp_file_path, std::ios::binary);
                out_log << temp_in_stream.rdbuf();
                temp_in_stream.close();
                std::filesystem::remove(temp_file_path);
            }
        }
        
        out_log.close();

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        
        // ... (The rest of the main function for reporting results is unchanged) ...
        std::cout << "\n--- Decompression Summary ---" << std::endl;
        std::cout << "Decompression complete." << std::endl;
        std::cout << "Total execution time: " << duration.count() << " ms" << std::endl;
        std::cout << "Output file is at: " << output_file << std::endl;
        uintmax_t total_compressed_size = 0;
        for(const auto& file : chunk_files) {
            if(std::filesystem::exists(file)) total_compressed_size += std::filesystem::file_size(file);
        }
        uintmax_t decompressed_size = std::filesystem::file_size(output_file);
        double compressed_mb = total_compressed_size / (1024.0 * 1024.0);
        double decompressed_mb = decompressed_size / (1024.0 * 1024.0);
        double time_s = duration.count() / 1000.0;
        double speed_mbps = (time_s > 0) ? (decompressed_mb / time_s) : 0.0;
        std::cout << "Total compressed size: " << std::fixed << std::setprecision(2) << compressed_mb << " MB\n";
        std::cout << "Decompressed size: " << std::fixed << std::setprecision(2) << decompressed_mb << " MB\n";
        std::cout << "Decompression Speed: " << std::fixed << std::setprecision(2) << speed_mbps << " MB/s\n";
        std::string logname = compressed_dir.filename().string();
        log_decompression_result(logname, num_threads, compressed_mb, decompressed_mb, duration.count(), speed_mbps);

    } catch (const std::exception& e) {
        std::cerr << "An unrecoverable error occurred: " << e.what() << std::endl;
        std::filesystem::remove_all(temp_base_path);
        return 1;
    }
    
    std::filesystem::remove_all(temp_base_path);
    return 0;
}