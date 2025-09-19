#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <fstream>
#include <filesystem>
#include <unordered_map>
#include <memory>
#include <algorithm>
#include <stdexcept>
#include <list>
#include <iomanip>
#include <cstdlib>
#include <set>
#include <chrono>
#include <future>
#include <sstream>
#include <thread>
#include <mutex>
#include <functional> // Added for std::function

// ===================================================================
// New dependency: libarchive
// ===================================================================
#include <archive.h>
#include <archive_entry.h>

// ===================================================================
// Helper Functions (Unchanged)
// ===================================================================

/**
 * @brief Decodes a variable-length encoded (varint) integer from a character vector buffer.
 * @param buffer The buffer containing the encoded data.
 * @param index A reference to the current reading position in the buffer, which will be updated by the function.
 * @return The decoded 64-bit signed integer.
 * @throw std::runtime_error If the buffer is exhausted before the integer is fully decoded.
 */
int64_t elastic_decode_from_buffer(const std::vector<char>& buffer, size_t& index) {
    uint64_t cur = 0;
    int shift = 0;
    while (index < buffer.size()) {
        unsigned char byte = buffer[index++];
        cur |= (static_cast<uint64_t>(byte & 0x7F) << shift);
        if ((byte & 0x80) == 0) { // MSB of 0 indicates the end of the integer.
            return (cur >> 1) ^ -(cur & 1); // ZigZag decoding.
        }
        shift += 7;
    }
    throw std::runtime_error("Incomplete variable-length integer encountered while decoding from buffer.");
}

/**
 * @brief Decodes a variable-length encoded (varint) integer from an input stream.
 * @param is The input stream.
 * @return The decoded 64-bit signed integer.
 * @throw std::runtime_error If the stream ends before the decoding is complete.
 */
int64_t elastic_decode(std::istream& is) {
    uint64_t cur = 0;
    int shift = 0;
    unsigned char byte;
    while (is.read(reinterpret_cast<char*>(&byte), 1)) {
        cur |= (static_cast<uint64_t>(byte & 0x7F) << shift);
        if ((byte & 0x80) == 0) break;
        shift += 7;
    }
    if (is.gcount() == 0) throw std::runtime_error("Encountered end-of-file while reading from stream, cannot decode.");
    if (!is && (byte & 0x80) != 0) throw std::runtime_error("Incomplete variable-length integer.");
    return (cur >> 1) ^ -(cur & 1); // ZigZag decoding.
}

/**
 * @brief Performs an inverse delta transform on a list of integers.
 * @param l The list of delta-encoded integers.
 * @return The original list of integers.
 */
std::list<int64_t> delta_inverse_transform(const std::list<int64_t>& l) {
    if (l.empty()) return {};
    std::list<int64_t> new_list;
    auto it = l.begin();
    int64_t last = *it; // The first number is the base value.
    new_list.push_back(last);
    for (++it; it != l.end(); ++it) {
        last = last + *it; // Subsequent numbers are the difference from the previous one.
        new_list.push_back(last);
    }
    return new_list;
}

// ===================================================================
// Optimization 1: Use libarchive instead of an external tar command
// ===================================================================
/**
 * @brief Extracts an archive file to a specified destination directory using the libarchive library.
 * @param archive_path Path to the archive file (e.g., .tar.xz).
 * @param dest_dir The destination directory for extraction.
 * @throw std::runtime_error If any error occurs during extraction.
 */
void extract_archive(const std::filesystem::path& archive_path, const std::filesystem::path& dest_dir) {
    struct archive *a;
    struct archive *ext;
    struct archive_entry *entry;
    int r;
    // Set extraction flags: preserve time, permissions, ACLs, and file flags.
    int flags = ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_ACL | ARCHIVE_EXTRACT_FFLAGS;

    a = archive_read_new();
    archive_read_support_format_all(a); // Support all archive formats.
    archive_read_support_filter_all(a); // Support all filters (e.g., xz, gz).

    ext = archive_write_disk_new();
    archive_write_disk_set_options(ext, flags);
    archive_write_disk_set_standard_lookup(ext);

    if ((r = archive_read_open_filename(a, archive_path.c_str(), 10240))) {
        archive_read_free(a);
        archive_write_free(ext);
        throw std::runtime_error("Failed to open archive file: " + std::string(archive_error_string(a)));
    }

    for (;;) {
        r = archive_read_next_header(a, &entry);
        if (r == ARCHIVE_EOF) break; // Break the loop if end of archive is reached.
        if (r < ARCHIVE_OK) {
             throw std::runtime_error("Failed to read header: " + std::string(archive_error_string(a)));
        }
        if (r < ARCHIVE_WARN) { // Fatal error.
             throw std::runtime_error("Fatal error while reading header: " + std::string(archive_error_string(a)));
        }

        // Construct the full path within the destination directory.
        const std::filesystem::path new_path = dest_dir / archive_entry_pathname(entry);
        // Update the entry's pathname so libarchive extracts it to the correct location.
        archive_entry_set_pathname(entry, new_path.c_str());

        r = archive_write_header(ext, entry); // Write the file header to disk.
        if (r < ARCHIVE_OK) {
            throw std::runtime_error("Failed to write header: " + std::string(archive_error_string(ext)));
        } else if (archive_entry_size(entry) > 0) { // If the file has content, write its data blocks.
            const void *buff;
            size_t size;
            int64_t offset;

            for (;;) {
                r = archive_read_data_block(a, &buff, &size, &offset); // Read a block of data from the archive.
                if (r == ARCHIVE_EOF) break;
                if (r < ARCHIVE_OK) {
                    throw std::runtime_error("Failed to read data block: " + std::string(archive_error_string(a)));
                }
                r = archive_write_data_block(ext, buff, size, offset); // Write the data block to the file on disk.
                if (r < ARCHIVE_OK) {
                     throw std::runtime_error("Failed to write data block: " + std::string(archive_error_string(ext)));
                }
            }
        }
        r = archive_write_finish_entry(ext); // Finalize the writing of the current file.
        if (r < ARCHIVE_OK) {
            throw std::runtime_error("Failed to finish entry write: " + std::string(archive_error_string(ext)));
        }
    }

    // Clean up and release resources.
    archive_read_close(a);
    archive_read_free(a);
    archive_write_close(ext);
    archive_write_free(ext);
}

// ===================================================================
// Data Provider Classes (Unchanged)
// ===================================================================

// Abstract base class for variable data providers.
class VariableDataProvider {
public:
    virtual ~VariableDataProvider() = default;
    virtual std::string getNextValue() = 0; // Gets the next variable value.
};

// Provider for numeric variables.
class NumericProvider : public VariableDataProvider {
    std::vector<std::string> data_; // Stores numbers decoded and converted to strings.
    size_t index_ = 0;

    void initialize_from_buffer(const std::vector<char>& buffer, bool is_delta) {
        std::list<int64_t> numbers;
        size_t buffer_idx = 0;
        while(buffer_idx < buffer.size()) {
            numbers.push_back(elastic_decode_from_buffer(buffer, buffer_idx));
        }
        if (is_delta) { // If delta encoded, perform inverse transform.
            numbers = delta_inverse_transform(numbers);
        }
        data_.reserve(numbers.size());
        for(const auto& num : numbers) {
            data_.push_back(std::to_string(num));
        }
    }

public:
    // Construct from a file.
    NumericProvider(const std::filesystem::path& file_path, bool is_delta) {
        std::ifstream file(file_path, std::ios::binary | std::ios::ate);
        if (!file) throw std::runtime_error("Failed to open numeric file: " + file_path.string());
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        std::vector<char> buffer(size);
        if (!file.read(buffer.data(), size)) {
            throw std::runtime_error("Failed to read numeric file: " + file_path.string());
        }
        file.close();
        initialize_from_buffer(buffer, is_delta);
    }
    
    // Construct from a memory buffer.
    NumericProvider(const std::vector<char>& buffer, bool is_delta) {
        initialize_from_buffer(buffer, is_delta);
    }

    std::string getNextValue() override {
        if (index_ >= data_.size()) throw std::out_of_range("NumericProvider data exhausted.");
        return data_[index_++];
    }
};

// Provider for dictionary/mapping type variables.
class MappingProvider : public VariableDataProvider {
private:
    std::vector<std::string> mapping_; // Dictionary content.
    std::vector<int> ids_;           // Sequence of value IDs.
    size_t index_ = 0;
public:
    // Construct from files.
    MappingProvider(const std::filesystem::path& mapping_file_path, const std::filesystem::path& ids_file_path) {
        std::ifstream mapping_file(mapping_file_path);
        if (!mapping_file) throw std::runtime_error("Failed to open mapping file: " + mapping_file_path.string());
        
        std::stringstream ss;
        ss << mapping_file.rdbuf();
        initialize_from_string(ss.str());
        mapping_file.close();

        std::ifstream ids_file(ids_file_path, std::ios::binary | std::ios::ate);
        if (!ids_file) throw std::runtime_error("Failed to open ID file: " + ids_file_path.string());
        std::streamsize size = ids_file.tellg();
        ids_file.seekg(0, std::ios::beg);
        std::vector<char> buffer(size);
        if (!ids_file.read(buffer.data(), size)) {
            throw std::runtime_error("Failed to read ID file: " + ids_file_path.string());
        }
        ids_file.close();
        initialize_ids_from_buffer(buffer);
    }
    
    // Construct from memory buffers.
    MappingProvider(std::string_view mapping_data, const std::vector<char>& ids_data) {
        initialize_from_string(mapping_data);
        initialize_ids_from_buffer(ids_data);
    }

    std::string getNextValue() override {
        if (index_ >= ids_.size()) throw std::out_of_range("MappingProvider data exhausted.");
        int id = ids_[index_++];
        if (id < 0 || (size_t)id >= mapping_.size()) throw std::runtime_error("Invalid ID: " + std::to_string(id));
        return mapping_[id];
    }
    const std::vector<std::string>& get_mapping() const { return mapping_; }
    const std::vector<int>& get_ids() const { return ids_; }

private:
    void initialize_from_string(std::string_view sv) {
        std::stringstream mapping_stream{std::string(sv)};
        std::string line;
        mapping_.push_back(""); // ID 0 maps to an empty string.
        while (std::getline(mapping_stream, line)) {
            mapping_.push_back(std::move(line));
        }
    }
    void initialize_ids_from_buffer(const std::vector<char>& buffer) {
        size_t buffer_idx = 0;
        while(buffer_idx < buffer.size()) {
            ids_.push_back(static_cast<int>(elastic_decode_from_buffer(buffer, buffer_idx)));
        }
    }
};

// Provider for variables where all values are the same (storage optimization).
class AllSameProvider : public VariableDataProvider {
    std::string value_;
public:
    // Construct from a file.
    AllSameProvider(const std::filesystem::path& file_path) {
        std::ifstream file(file_path, std::ios::binary);
        if (!file) throw std::runtime_error("Failed to open .allsame file: " + file_path.string());
        value_ = std::to_string(elastic_decode(file));
    }
    
    // Construct from a memory buffer.
    AllSameProvider(const std::vector<char>& buffer) {
        if (buffer.empty()) {
            value_ = "[AllSameProvider Error: Buffer is empty]";
            return;
        }
        size_t index = 0;
        value_ = std::to_string(elastic_decode_from_buffer(buffer, index));
    }

    std::string getNextValue() override { return value_; }
};


// ===================================================================
// Core Decompressor Structures
// ===================================================================

// Index information for "fast mode".
struct FastIndexInfo {
    size_t mapping_offset; // Offset in all_mappings.fast.txt
    size_t mapping_size;   // Size of the mapping data.
    size_t ids_offset;     // Offset in all_ids.fast.bin
    size_t ids_size;       // Size of the ID data.
};

// Index information for "normal mode".
struct NormalIndexInfo {
    std::string storage_type; // Storage type (MAPPING, NUMERIC_DELTA, etc.).
    size_t file1_offset;      // Offset in the aggregated data file.
    size_t file1_size;        // Size of the data.
    size_t file2_offset;      // Offset for a second file (e.g., mapping file).
    size_t file2_size;        // Size of the second file's data.
};

// A pre-parsed piece of a template.
struct ParsedTemplatePiece {
    bool is_variable; // True if this piece is a variable.
    std::string data; // The compact_id if a variable, or the static text otherwise.
};

// ===================================================================
// Decompressor Class Definition (Modified)
// ===================================================================
class Decompressor {
public:
    Decompressor(const std::filesystem::path& archive_path, const std::filesystem::path& temp_base_path);
    ~Decompressor();
    void decompress_to_stream(std::ostream& out);

private:
    std::filesystem::path temp_dir_path_; // Path to the temporary directory for intermediate files.
    std::string logname_;                 // Log type name inferred from filenames (e.g., "OpenSSH", "Hadoop").
    std::unordered_map<std::string, std::string> id_to_full_tag_; // Maps compact ID -> original full tag string (e.g., "0" -> "<T>").
    std::vector<int> template_id_sequence_; // Sequence of template IDs, defining the order of log line reconstruction.
    std::unordered_map<std::string, std::unique_ptr<VariableDataProvider>> data_providers_; // Maps compact ID -> pointer to its data provider.
    std::vector<std::vector<ParsedTemplatePiece>> preparsed_templates_; // All templates, pre-parsed for fast reconstruction.
    bool is_fast_mode_ = false; // Flag indicating if the "fast mode" optimization path is used.

    // Decomposes all template strings into static text and variable placeholders.
    void preparse_all_templates(const std::vector<std::string>& raw_templates);
    // Parses structured tags, like "<STR=...|LEN=...>".
    std::unordered_map<std::string, std::string> parse_structured_tag(std::string_view tag);
    
    // --- Core Refactoring Start ---
    // Define a function type for handlers of tags with special reconstruction logic.
    using SpecialHandler = std::function<std::string(const std::string&)>;
    // Stores handlers for special tags (e.g., <T>, <I>).
    std::unordered_map<std::string, SpecialHandler> special_handlers_;
    // Caches parsed structured tags to avoid redundant parsing.
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> tag_parse_cache_;
    
    // Initializes handlers for all special tags.
    void initialize_special_handlers();
    // Reconstructs the original token from its stored value and tag information.
    std::string reconstruct_token_from_value(const std::string& full_tag, const std::string& stored_value);
    // --- Core Refactoring End ---
};

// ===================================================================
// Decompressor Constructor & Destructor (Modified)
// ===================================================================
Decompressor::Decompressor(const std::filesystem::path& archive_path, const std::filesystem::path& temp_base_path)
    : temp_dir_path_(temp_base_path / archive_path.stem().string()) {
    // Clean up and create the temporary directory.
    if (std::filesystem::exists(temp_dir_path_)) {
        std::filesystem::remove_all(temp_dir_path_);
    }
    std::filesystem::create_directories(temp_dir_path_);

    try {
        // 1. Extract the main archive file into the temporary directory.
        extract_archive(archive_path, temp_dir_path_);
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to extract archive (" + archive_path.string() + "): " + e.what());
    }

    // 2. Parse tags_mapping.txt to build the map from compact ID to full tag.
    std::ifstream tags_map_file(temp_dir_path_ / "tags_mapping.txt");
    if(!tags_map_file) throw std::runtime_error("Could not find tags_mapping.txt");
    std::string line;
    while (std::getline(tags_map_file, line)) {
        size_t sep = line.find(':');
        if (sep != std::string::npos) {
            id_to_full_tag_[line.substr(0, sep)] = line.substr(sep + 1);
        }
    }

    // 3. Find template files to determine the log type (logname).
    std::filesystem::path template_mapping_path, template_ids_path;
    const std::string mapping_suffix = "_templates.mapping.txt";
    const std::string ids_suffix = "_templates.ids.bin";

    for (const auto& entry : std::filesystem::directory_iterator(temp_dir_path_)) {
        const std::string filename = entry.path().filename().string();
        if (filename.length() > mapping_suffix.length() && filename.substr(filename.length() - mapping_suffix.length()) == mapping_suffix) {
            template_mapping_path = entry.path();
            logname_ = filename.substr(0, filename.length() - mapping_suffix.length());
        } else if (filename.length() > ids_suffix.length() && filename.substr(filename.length() - ids_suffix.length()) == ids_suffix) {
            template_ids_path = entry.path();
        }
    }
    
    if (logname_.empty()) {
        std::cerr << "Warning: Could not find template files in " << temp_dir_path_ << " to determine logname." << std::endl;
    }
    
    // *** NEW: Initialize special handlers after determining logname_ ***
    initialize_special_handlers();

    if (template_mapping_path.empty() || template_ids_path.empty()) {
        throw std::runtime_error("Could not find template mapping/ID files.");
    }

    // 4. Load template mappings and the sequence of template IDs.
    MappingProvider template_provider(template_mapping_path, template_ids_path);
    const auto& raw_templates = template_provider.get_mapping();
    template_id_sequence_ = template_provider.get_ids();

    // 5. Pre-parse all templates for faster reconstruction later.
    preparse_all_templates(raw_templates);

    const auto processed_tags_dir = temp_dir_path_ / "processed_tags";
    if (!std::filesystem::exists(processed_tags_dir)) return;

    const auto fast_index_path = processed_tags_dir / "index.fast.csv";
    const auto normal_index_path = processed_tags_dir / "index.normal.csv";

    // 6. Load variable data: check for "fast mode" index first.
    if (std::filesystem::exists(fast_index_path)) {
        is_fast_mode_ = true;
        std::cout << "  (Fast mode detected, using fast decompression path...)" << std::endl;
        
        // Read the "fast mode" index file.
        std::unordered_map<std::string, FastIndexInfo> fast_index;
        std::ifstream index_file(fast_index_path);
        if (!index_file) throw std::runtime_error("Failed to open fast mode index file: " + fast_index_path.string());
        
        std::string index_line;
        std::getline(index_file, index_line); // Skip header.
        while(std::getline(index_file, index_line)) {
            std::stringstream ss(index_line);
            std::string tag_id, map_offset, map_size, ids_offset, ids_size;
            std::getline(ss, tag_id, ',');
            std::getline(ss, map_offset, ',');
            std::getline(ss, map_size, ',');
            std::getline(ss, ids_offset, ',');
            std::getline(ss, ids_size, ',');
            try {
                fast_index[tag_id] = {std::stoull(map_offset), std::stoull(map_size), std::stoull(ids_offset), std::stoull(ids_size)};
            } catch (const std::exception& e) {
                throw std::runtime_error("Failed to parse Fast index line '" + index_line + "': " + e.what());
            }
        }
        index_file.close();

        // Read all mapping data into memory at once.
        std::string all_mappings_content;
        auto mapping_data_path = processed_tags_dir / "all_mappings.fast.txt";
        std::ifstream map_data_file(mapping_data_path, std::ios::binary | std::ios::ate);
        if (!map_data_file) throw std::runtime_error("Failed to open fast mode mapping data file: " + mapping_data_path.string());
        all_mappings_content.resize(map_data_file.tellg());
        map_data_file.seekg(0);
        map_data_file.read(all_mappings_content.data(), all_mappings_content.size());
        map_data_file.close();

        // Read all ID data into memory at once.
        std::vector<char> all_ids_content;
        auto ids_data_path = processed_tags_dir / "all_ids.fast.bin";
        std::ifstream ids_data_file(ids_data_path, std::ios::binary | std::ios::ate);
        if (!ids_data_file) throw std::runtime_error("Failed to open fast mode ID data file: " + ids_data_path.string());
        all_ids_content.resize(ids_data_file.tellg());
        ids_data_file.seekg(0);
        ids_data_file.read(all_ids_content.data(), all_ids_content.size());
        ids_data_file.close();

        // Create MappingProviders for each variable from the large memory blocks using the index info.
        for (const auto& [tag_id, info] : fast_index) {
            if (info.mapping_size == 0 && info.ids_size == 0) continue;
            std::string_view mapping_sv(all_mappings_content.data() + info.mapping_offset, info.mapping_size);
            std::vector<char> ids_segment(all_ids_content.begin() + info.ids_offset, all_ids_content.begin() + info.ids_offset + info.ids_size);
            data_providers_[tag_id] = std::make_unique<MappingProvider>(mapping_sv, ids_segment);
        }

    } else if (std::filesystem::exists(normal_index_path)) {
        // 6a. (New) Normal Mode: Use aggregated files and an index.
        is_fast_mode_ = false;
        std::cout << "  (New Normal mode detected, using aggregated file decompression path...)" << std::endl;

        // Read the "normal mode" index file.
        std::unordered_map<std::string, NormalIndexInfo> normal_index;
        std::ifstream index_file(normal_index_path);
        if (!index_file) throw std::runtime_error("Failed to open normal mode index file: " + normal_index_path.string());
        
        std::string index_line;
        std::getline(index_file, index_line); // Skip header.
        while(std::getline(index_file, index_line)) {
            std::stringstream ss(index_line);
            std::string tag_id, storage_type, f1_off, f1_size, f2_off, f2_size;
            std::getline(ss, tag_id, ',');
            std::getline(ss, storage_type, ',');
            std::getline(ss, f1_off, ',');
            std::getline(ss, f1_size, ',');
            std::getline(ss, f2_off, ',');
            std::getline(ss, f2_size, ',');
            try {
                normal_index[tag_id] = {storage_type, std::stoull(f1_off), std::stoull(f1_size), std::stoull(f2_off), std::stoull(f2_size)};
            } catch (const std::exception& e) {
                throw std::runtime_error("Failed to parse Normal index line '" + index_line + "': " + e.what());
            }
        }
        index_file.close();

        // Read all mapping and variable data into memory.
        std::string all_mappings_content;
        std::vector<char> all_variables_content;

        auto mapping_data_path = processed_tags_dir / "all_mappings.normal.txt";
        std::ifstream map_data_file(mapping_data_path, std::ios::binary | std::ios::ate);
        if (!map_data_file) throw std::runtime_error("Failed to open normal mode mapping data file: " + mapping_data_path.string());
        all_mappings_content.resize(map_data_file.tellg());
        map_data_file.seekg(0);
        map_data_file.read(all_mappings_content.data(), all_mappings_content.size());
        map_data_file.close();

        auto var_data_path = processed_tags_dir / "all_variables.normal.bin";
        std::ifstream var_data_file(var_data_path, std::ios::binary | std::ios::ate);
        if (!var_data_file) throw std::runtime_error("Failed to open normal mode variable data file: " + var_data_path.string());
        all_variables_content.resize(var_data_file.tellg());
        var_data_file.seekg(0);
        var_data_file.read(all_variables_content.data(), all_variables_content.size());
        var_data_file.close();
        
        // Create data providers for each variable from memory segments.
        for (const auto& [tag_id, info] : normal_index) {
            std::vector<char> var_segment(all_variables_content.begin() + info.file1_offset, all_variables_content.begin() + info.file1_offset + info.file1_size);

            if (info.storage_type == "MAPPING") {
                std::string_view mapping_sv(all_mappings_content.data() + info.file2_offset, info.file2_size);
                data_providers_[tag_id] = std::make_unique<MappingProvider>(mapping_sv, var_segment);
            } else if (info.storage_type == "NUMERIC_DELTA") {
                data_providers_[tag_id] = std::make_unique<NumericProvider>(var_segment, true);
            } else if (info.storage_type == "NUMERIC_NODELTA") {
                data_providers_[tag_id] = std::make_unique<NumericProvider>(var_segment, false);
            } else if (info.storage_type == "ALLSAME") {
                data_providers_[tag_id] = std::make_unique<AllSameProvider>(var_segment);
            }
        }

    } else {
        // 6b. (Legacy) Fallback mode: load data from individual files.
        is_fast_mode_ = false;
        std::cout << "  (No index file found, using legacy per-file decompression path...)" << std::endl;
        const std::string delta_suffix = ".numeric.delta.bin";
        const std::string nodelta_suffix = ".numeric.nodelta.bin";
        const std::string allsame_suffix = ".allsame.bin";
        const std::string var_ids_suffix = ".ids.bin";
        for (const auto& entry : std::filesystem::directory_iterator(processed_tags_dir)) {
            const auto& path = entry.path();
            const std::string filename = path.filename().string();
            std::string compact_id = path.stem().string();

            size_t first_dot = compact_id.find('.');
            if (first_dot != std::string::npos) {
                compact_id = compact_id.substr(0, first_dot);
            }

            if (data_providers_.count(compact_id)) continue;

            if (filename.length() >= delta_suffix.length() && filename.substr(filename.length() - delta_suffix.length()) == delta_suffix) {
                data_providers_[compact_id] = std::make_unique<NumericProvider>(path, true);
            } else if (filename.length() >= nodelta_suffix.length() && filename.substr(filename.length() - nodelta_suffix.length()) == nodelta_suffix) {
                data_providers_[compact_id] = std::make_unique<NumericProvider>(path, false);
            } else if (filename.length() >= allsame_suffix.length() && filename.substr(filename.length() - allsame_suffix.length()) == allsame_suffix) {
                data_providers_[compact_id] = std::make_unique<AllSameProvider>(path);
            } else if (filename.length() >= var_ids_suffix.length() && filename.substr(filename.length() - var_ids_suffix.length()) == var_ids_suffix) {
                auto mapping_path = path.parent_path() / (compact_id + ".mapping.txt");
                if (std::filesystem::exists(mapping_path)) {
                    data_providers_[compact_id] = std::make_unique<MappingProvider>(mapping_path, path);
                }
            }
        }
    }
}

Decompressor::~Decompressor() {
    std::error_code ec;
    std::filesystem::remove_all(temp_dir_path_, ec);
}

// ===================================================================
// Decompressor Member Functions (Partially Unchanged, Partially Refactored)
// ===================================================================

void Decompressor::preparse_all_templates(const std::vector<std::string>& raw_templates) {
    preparsed_templates_.resize(raw_templates.size());
    for (size_t i = 0; i < raw_templates.size(); ++i) {
        if (i == 0) { // Template 0 is always empty.
             preparsed_templates_[i] = {};
             continue;
        }
        std::string_view template_sv = raw_templates[i];
        size_t last_pos = 0;
        size_t start_tag;
        while ((start_tag = template_sv.find('<', last_pos)) != std::string::npos) {
            // Add static text before the tag.
            if (start_tag > last_pos) {
                preparsed_templates_[i].push_back({false, std::string(template_sv.substr(last_pos, start_tag - last_pos))});
            }
            size_t end_tag = template_sv.find('>', start_tag);
            // Handle unterminated tags.
            if (end_tag == std::string::npos) {
                preparsed_templates_[i].push_back({false, std::string(template_sv.substr(start_tag))});
                last_pos = template_sv.length();
                break;
            }
            // Add variable placeholder.
            std::string compact_id(template_sv.substr(start_tag + 1, end_tag - start_tag - 1));
            preparsed_templates_[i].push_back({true, std::move(compact_id)});
            last_pos = end_tag + 1;
        }
        // Add any remaining static text after the last tag.
        if (last_pos < template_sv.length()) {
            preparsed_templates_[i].push_back({false, std::string(template_sv.substr(last_pos))});
        }
    }
}

// ===================================================================
// Decompressor::decompress_to_stream (Comments updated to English)
// ===================================================================

void Decompressor::decompress_to_stream(std::ostream& out) {
    // Iterate through the sequence of template IDs to reconstruct each log line.
    for (int template_id : template_id_sequence_) {
        if (template_id == 0) {
            // template_id 0 is reserved for empty lines, a convention from the compression process.
            out << "\n";
            continue;
        }
        if (template_id < 0 || (size_t)template_id >= preparsed_templates_.size()) {
            // Handle cases where the template ID is out of bounds.
            out << "[Invalid Template ID:" << template_id << "]\n";
            continue;
        }

        const auto& pieces = preparsed_templates_[template_id];
        std::string result_line;
        result_line.reserve(256); // Pre-allocate memory for the line to improve performance.

        // Reconstruct the line piece by piece.
        for (const auto& piece : pieces) {
            if (piece.is_variable) {
                // This piece is a variable placeholder.
                const std::string& compact_id = piece.data;
                auto it_provider = data_providers_.find(compact_id);
                
                if (it_provider != data_providers_.end()) {
                    // A data provider was found, which is the expected flow.
                    try {
                        auto it_tag = id_to_full_tag_.find(compact_id);
                        if (it_tag != id_to_full_tag_.end()) {
                            // Use the retrieved value and full tag information to reconstruct the original token.
                            result_line.append(reconstruct_token_from_value(it_tag->second, it_provider->second->getNextValue()));
                        } else {
                            // This is an internal inconsistency: data exists but there's no tag definition.
                            result_line.append("[Missing full tag definition for " + compact_id + "]");
                        }
                    } catch (const std::out_of_range&) {
                        // The data provider has run out of data, which might indicate a corrupt file or compression error.
                        result_line.append("[Data provider exhausted for: " + compact_id + "]");
                    }
                } else {
                    // ====================================================================
                    //  *** CORE CHANGE: Handling non-variable tags ***
                    // ====================================================================
                    // If no data provider is found for the content within <...>, we
                    // no longer treat it as an error. Instead, we assume this <...>
                    // structure is literal text from the original log (e.g., a line
                    // might contain "<stdin>" or "<init>").
                    // Therefore, we just reconstruct the original <...> form and
                    // append it to the result line.
                    result_line.append("<").append(compact_id).append(">");
                }
            } else {
                // This piece is static text, so just append it directly.
                result_line.append(piece.data);
            }
        }
        out << result_line << "\n";
    }
}

// *** NEW: Special tag handler initialization function ***
void Decompressor::initialize_special_handlers() {
    // Handle the <T> tag (timestamp), with different logic based on logname_.
    special_handlers_["<T>"] = [this](const std::string& stored_value) -> std::string {
            if (logname_ == "OpenSSH") {
                const std::string& value = stored_value;
                size_t len = value.length();
                if (len == 7 || len == 8) {
                    try {
                        size_t day_part_len = len - 6;
                        int dd = std::stoi(value.substr(0, day_part_len));
                        int hh = std::stoi(value.substr(day_part_len, 2));
                        int mm = std::stoi(value.substr(day_part_len + 2, 2));
                        int ss = std::stoi(value.substr(day_part_len + 4, 2));
                        std::stringstream result;
                        result << dd << " " << std::setw(2) << std::setfill('0') << hh << ":" << std::setw(2) << std::setfill('0') << mm << ":" << std::setw(2) << std::setfill('0') << ss;
                        return result.str();
                    } catch (const std::exception&) {
                        return "[OpenSSH timestamp conversion error: " + stored_value + "]";
                    }
                } else {
                    return "[OpenSSH timestamp format error (length should be 7 or 8): " + stored_value + "]";
                }
            }
            else if (logname_ == "Apache") {
                std::string padded_value = stored_value;
                while (padded_value.length() < 8) {
                    padded_value.insert(0, 1, '0');
                }
                if (padded_value.length() == 8) {
                    return padded_value.substr(0, 2) + " " + padded_value.substr(2, 2) + ":" + padded_value.substr(4, 2) + ":" + padded_value.substr(6, 2);
                } else {
                    return "[Apache timestamp format error: " + stored_value + "]";
                }
            }
            else if (logname_ == "Android") {
                std::string padded_value = stored_value;
                if (padded_value.length() == 12) {
                    padded_value.insert(0, 1, '0');
                }
                if (padded_value.length() == 13) {
                    return padded_value.substr(0, 2) + "-" + padded_value.substr(2, 2) + " " +
                        padded_value.substr(4, 2) + ":" + padded_value.substr(6, 2) + ":" +
                        padded_value.substr(8, 2) + "." + padded_value.substr(10, 3);
                } else {
                    return "[Android timestamp format error (length should be 12 or 13): " + stored_value + "]";
                }
            }
            else if (logname_ == "Hadoop" || logname_ == "HDFS") {
                if (stored_value.length() == 9) {
                    return stored_value.substr(0, 2) + ":" + stored_value.substr(2, 2) + ":" +
                        stored_value.substr(4, 2) + "," + stored_value.substr(6, 3);
                } else {
                    return "[" + logname_ + " timestamp format error (length should be 9): " + stored_value + "]";
                }
            }
            else if (logname_ == "HPC") {
                std::string padded_value = stored_value;
                while(padded_value.length() < 10) {
                    padded_value.insert(0, 1, '0');
                }
                return padded_value;
            }
            // Linux & Mac:  hh:mm:ss
            else if (logname_ == "Linux" || logname_ == "Mac") {
                // 
                if (stored_value.length() > 6) {
                    return "[" + logname_ + " timestamp format error (length > 6): " + stored_value + "]";
                }
                
                std::string padded_value = stored_value;
                // 
                while (padded_value.length() < 6) {
                    padded_value.insert(0, 1, '0');
                }
                
                // 
                return padded_value.substr(0, 2) + ":" + padded_value.substr(2, 2) + ":" + padded_value.substr(4, 2);
            }
            else if (logname_ == "Windows") {
                if (stored_value.length() == 14) {
                    return stored_value.substr(0, 4) + "-" + stored_value.substr(4, 2) + "-" +
                        stored_value.substr(6, 2) + " " + stored_value.substr(8, 2) + ":" +
                        stored_value.substr(10, 2) + ":" + stored_value.substr(12, 2);
                } else {
                    return "[Windows timestamp format error (length should be 14): " + stored_value + "]";
                }
            }
            else if (logname_ == "Zookeeper") {
                if (stored_value.length() == 17) {
                    return stored_value.substr(0, 4) + "-" + stored_value.substr(4, 2) + "-" +
                        stored_value.substr(6, 2) + " " + stored_value.substr(8, 2) + ":" +
                        stored_value.substr(10, 2) + ":" + stored_value.substr(12, 2) + "," +
                        stored_value.substr(14, 3);
                } else {
                    return "[Zookeeper timestamp format error (length should be 17): " + stored_value + "]";
                }
            }
            else if (logname_ == "Proxifier") {
                std::string padded_value = stored_value;
                if (padded_value.length() == 9) {
                    padded_value.insert(0, 1, '0');
                }
                if (padded_value.length() == 10) {
                    return padded_value.substr(0, 2) + "." + padded_value.substr(2, 2) + " " +
                        padded_value.substr(4, 2) + ":" + padded_value.substr(6, 2) + ":" +
                        padded_value.substr(8, 2);
                } else {
                    return "[Proxifier timestamp format error (length should be 9 or 10): " + stored_value + "]";
                }
            }
            else { // Default/generic timestamp logic.
                std::string padded_value = stored_value;
                while(padded_value.length() < 14) padded_value.insert(0, 1, '0');
                if (padded_value.length() == 14) {
                    return padded_value.substr(0, 4) + "-" + padded_value.substr(4, 2) + "-" + padded_value.substr(6, 2) + " " + padded_value.substr(8, 2) + ":" + padded_value.substr(10, 2) + ":" + padded_value.substr(12, 2);
                } else if (padded_value.length() == 12) {
                    return "20" + padded_value.substr(0, 2) + "-" + padded_value.substr(2, 2) + "-" + padded_value.substr(4, 2) + " " + padded_value.substr(6, 2) + ":" + padded_value.substr(8, 2) + ":" + padded_value.substr(10, 2);
                }
                return "[Timestamp format error: " + stored_value + "]";
            }
        };
    // Handle the <I> tag (IP address).
    special_handlers_["<I>"] = [](const std::string& stored_value) -> std::string {
        std::string padded_value = stored_value;
        while (padded_value.length() < 12) padded_value.insert(0, 1, '0');
        if (padded_value.length() != 12) return "[IP Data Error: value length is not 12 " + stored_value + "]";
        
        auto unpad_part = [](std::string_view part) -> std::string {
            size_t first_digit = part.find_first_not_of('0');
            return (first_digit == std::string_view::npos) ? "0" : std::string(part.substr(first_digit));
        };

        return unpad_part(std::string_view(padded_value).substr(0, 3)) + "." +
               unpad_part(std::string_view(padded_value).substr(3, 3)) + "." +
               unpad_part(std::string_view(padded_value).substr(6, 3)) + "." +
               unpad_part(std::string_view(padded_value).substr(9, 3));
    };

    // Handle tags that directly return the stored value.
    auto return_as_is = [](const std::string& stored_value) { return stored_value; };
    special_handlers_["<X>"] = return_as_is;
    special_handlers_["<Y>"] = return_as_is;
    special_handlers_["<Z>"] = return_as_is;
    special_handlers_["<B>"] = return_as_is;
    special_handlers_["<M>"] = return_as_is;
    special_handlers_["<K>"] = return_as_is;
    special_handlers_["<G>"] = return_as_is;
    special_handlers_["<S>"] = return_as_is;
    special_handlers_["<E>"] = return_as_is;
    special_handlers_["<F>"] = return_as_is;
    special_handlers_["<A>"] = return_as_is;
}

// *** REMOVED: Standalone restoreTimestamp function; its logic is integrated into initialize_special_handlers. ***

// *** REFACTORED: reconstruct_token_from_value function ***
std::string Decompressor::reconstruct_token_from_value(const std::string& full_tag, const std::string& stored_value) {
    // 1. Add a fast path for "fast mode".
    if (is_fast_mode_) {
        return stored_value;
    }

    // --- The following is the refactored logic for "Normal mode" ---

    // 2. Find and execute predefined handlers for tags with special reconstruction logic.
    auto it = special_handlers_.find(full_tag);
    if (it != special_handlers_.end()) {
        // Handler found, call it and return the result.
        return it->second(stored_value);
    }
    
    // 3. Handle empty values.
    if (stored_value.empty()) {
        return "";
    }
    
    // 4. If the tag is not structured (contains no '='), return the value directly.
    //    Since special tags were handled above, this is for unknown simple tags.
    if (full_tag.find('=') == std::string::npos) {
        return stored_value;
    }

    // 5. Handle generic structured tags (e.g., LEN=..., STR=...).
    const std::unordered_map<std::string, std::string>* tag_parts_ptr;
    auto cache_it = tag_parse_cache_.find(full_tag);
    if (cache_it != tag_parse_cache_.end()) {
        tag_parts_ptr = &cache_it->second;
    } else {
        auto parsed_parts = parse_structured_tag(full_tag);
        auto [inserted_it, _] = tag_parse_cache_.emplace(full_tag, std::move(parsed_parts));
        tag_parts_ptr = &inserted_it->second;
    }
    const auto& tag_parts = *tag_parts_ptr;

    // Handle LEN=...
    auto it_len = tag_parts.find("LEN");
    if (it_len != tag_parts.end()) {
        try {
            size_t required_len = std::stoul(it_len->second);
            std::string result = stored_value;
            if (result.length() < required_len) {
                result.insert(0, required_len - result.length(), '0');
            }
            return result;
        } catch (const std::exception&) {
            return "[LEN format error:" + stored_value + "]";
        }
    }

    // Handle STR=...
    auto it_str = tag_parts.find("STR");
    if (it_str != tag_parts.end() && it_str->second.find("\\d") != std::string::npos) {
        std::string_view structure = it_str->second;
        std::string padded_stored_value = stored_value;
        
        size_t required_digits = 0;
        for (size_t i = 0; i < structure.length(); ++i) {
            if (structure[i] == '\\' && i + 1 < structure.length() && structure[i+1] == 'd') {
                if (i + 2 < structure.length() && structure[i+2] == '{') {
                    size_t end_brace = structure.find('}', i + 3);
                    if (end_brace != std::string::npos) {
                        try {
                            required_digits += std::stoi(std::string(structure.substr(i + 3, end_brace - (i + 3))));
                        } catch(...) { /* ignore parse error */ }
                        i = end_brace;
                    }
                }
            }
        }

        if (required_digits > 0 && padded_stored_value.length() < required_digits) {
            padded_stored_value.insert(0, required_digits - padded_stored_value.length(), '0');
        }

        std::string result_token;
        result_token.reserve(structure.length() + padded_stored_value.length());
        size_t value_idx = 0;
        for (size_t i = 0; i < structure.length(); ++i) {
            if (structure[i] == '\\' && i + 1 < structure.length()) {
                i++;
                if (structure[i] == 'd') {
                    int len_to_read = 0;
                    size_t end_brace = std::string::npos;
                    if (i + 1 < structure.length() && structure[i+1] == '{') {
                        end_brace = structure.find('}', i + 2);
                        if (end_brace != std::string::npos) {
                            try {
                                len_to_read = std::stoi(std::string(structure.substr(i + 2, end_brace - (i + 2))));
                            } catch(...) { len_to_read = 0; }
                        }
                    }
                    if (len_to_read > 0) {
                         if (value_idx + static_cast<size_t>(len_to_read) > padded_stored_value.length()) {
                            return "[Data mismatch error: not enough digits to fill " + std::string(structure) + " in " + full_tag + "]";
                        }
                        result_token.append(padded_stored_value, value_idx, len_to_read);
                        value_idx += len_to_read;
                        i = end_brace;
                    } else {
                        result_token.push_back('d');
                    }
                } else {
                     result_token.push_back(structure[i]);
                }
            } else {
                result_token.push_back(structure[i]);
            }
        }
        if (value_idx < padded_stored_value.length()) {
            result_token.append(padded_stored_value.substr(value_idx));
        }
        return result_token;
    }

    // Fallback if no other logic matches.
    return stored_value;
}

std::unordered_map<std::string, std::string> Decompressor::parse_structured_tag(std::string_view tag) {
    std::unordered_map<std::string, std::string> parts;
    if (tag.length() < 2 || tag.front() != '<' || tag.back() != '>') return parts;
    tag.remove_prefix(1);
    tag.remove_suffix(1);
    size_t last = 0, next = 0;
    while ((next = tag.find('|', last)) != std::string::npos) {
        std::string_view part = tag.substr(last, next - last);
        size_t eq_pos = part.find('=');
        if (eq_pos != std::string::npos) parts[std::string(part.substr(0, eq_pos))] = std::string(part.substr(eq_pos + 1));
        last = next + 1;
    }
    std::string_view part = tag.substr(last);
    size_t eq_pos = part.find('=');
    if (eq_pos != std::string::npos) parts[std::string(part.substr(0, eq_pos))] = std::string(part.substr(eq_pos + 1));
    if (parts.empty() && tag.find('=') == std::string::npos) parts["_LEGACY_"] = std::string(tag);
    return parts;
}


// ===================================================================
// Main Function and Other Helpers (Unchanged)
// ===================================================================

bool ends_with(const std::string& str, const std::string& suffix) {
    return str.size() >= suffix.size() &&
           str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}


std::stringstream decompress_chunk_to_memory(const std::filesystem::path& chunk_path, const std::filesystem::path& temp_base_path) {
    std::stringstream memory_stream;
    try {
        Decompressor decompressor(chunk_path, temp_base_path);
        decompressor.decompress_to_stream(memory_stream);
    } catch (const std::exception& e) {
        memory_stream.str("");
        memory_stream << "[ERROR: Failed to process " << chunk_path.filename().string() << ": " << e.what() << "]\n";
        std::cerr << "Error processing chunk " << chunk_path.string() << ": " << e.what() << std::endl;
    }
    return memory_stream;
}

static std::mutex result_file_mutex;

void log_decompression_result(
    const std::string& logname,
    int num_threads,
    double compressed_size_mb,
    double decompressed_size_mb,
    long long execution_time_ms,
    double decompression_speed_mbps)
{
    std::lock_guard<std::mutex> lock(result_file_mutex);

    const std::string result_filename = "decompression_results.csv";
    bool file_exists = std::filesystem::exists(result_filename);

    std::ofstream result_file(result_filename, std::ios::app);
    if (!result_file.is_open()) {
        std::cerr << "Error: Could not open results file: " << result_filename << std::endl;
        return;
    }

    if (!file_exists || result_file.tellp() == 0) {
        result_file << "Timestamp,"
                    << "LogName,"
                    << "NumThreads,"
                    << "CompressedSize_MB,"
                    << "DecompressedSize_MB,"
                    << "ExecutionTime_ms,"
                    << "DecompressionSpeed_MBps\n";
    }

    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %X");

    result_file << ss.str() << ","
                << logname << ","
                << num_threads << ","
                << std::fixed << std::setprecision(4) << compressed_size_mb << ","
                << std::fixed << std::setprecision(4) << decompressed_size_mb << ","
                << execution_time_ms << ","
                << std::fixed << std::setprecision(2) << decompression_speed_mbps << "\n";
    
    std::cout << "Decompression results logged to " << result_filename << std::endl;
}



int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <compressed_dir> <output_log_file> <num_threads>\n";
        std::cerr << "Example: " << argv[0] << " output/OpenSSH decompressed_OpenSSH.log 8\n";
        std::cerr << "Note: Requires linking with libarchive (e.g., g++ -std=c++17 ... -larchive)\n";
        return 1;
    }

    const std::filesystem::path compressed_dir(argv[1]);
    const std::filesystem::path output_file(argv[2]);
    int num_threads;
    try {
        num_threads = std::stoi(argv[3]);
        if (num_threads <= 0) throw std::invalid_argument("Number of threads must be positive.");
    } catch(const std::exception& e) {
        std::cerr << "Invalid number of threads: " << e.what() << std::endl;
        return 1;
    }
    const std::filesystem::path temp_base_path = "decompress_temp";

    if (!std::filesystem::is_directory(compressed_dir)) {
        std::cerr << "Error: Not a directory: " << compressed_dir << std::endl;
        return 1;
    }
    if (std::filesystem::exists(temp_base_path)) std::filesystem::remove_all(temp_base_path);
    std::filesystem::create_directories(temp_base_path);
    if (std::filesystem::exists(output_file)) std::filesystem::remove(output_file);
    std::ofstream out_log(output_file, std::ios::binary);
    if (!out_log) {
        std::cerr << "Error: Cannot open output file: " << output_file << std::endl;
        return 1;
    }

    try {
        std::vector<std::filesystem::path> chunk_files;
        for (const auto& entry : std::filesystem::directory_iterator(compressed_dir)) {
            if (entry.is_regular_file() && entry.path().filename().string().rfind("chunk_", 0) == 0) {
                const std::string filename_str = entry.path().filename().string();
                if (ends_with(filename_str, ".tar.xz") || ends_with(filename_str, ".tar.gz") ||
                    ends_with(filename_str, ".tar.bz2") || ends_with(filename_str, ".tar.lz4") ||
                    ends_with(filename_str, ".tar")) {
                    chunk_files.push_back(entry.path());
                }
            }
        }

        std::sort(chunk_files.begin(), chunk_files.end(), [](const auto& a, const auto& b){
            try {
                auto get_id = [](const std::string& s) {
                    size_t start = s.find('_');
                    if (start == std::string::npos) return -1;
                    start++;
                    size_t end = s.find_first_not_of("0123456789", start);
                    return std::stoi(s.substr(start, end - start));
                };
                 return get_id(a.filename().string()) < get_id(b.filename().string());
            } catch(...) {
                return a.string() < b.string();
            }
        });

        auto start_time = std::chrono::high_resolution_clock::now();
        std::cout << "Found " << chunk_files.size() << " chunks. Decompressing using " << num_threads << " threads..." << std::endl;
        
        std::vector<std::future<std::stringstream>> futures;
        
        for (const auto& chunk_path : chunk_files) {
             if (futures.size() >= (size_t)num_threads) {
                 out_log << futures.front().get().rdbuf();
                 futures.erase(futures.begin());
             }
             futures.push_back(std::async(std::launch::async, decompress_chunk_to_memory, chunk_path, temp_base_path / chunk_path.stem().string()));
        }

        for(auto& fut : futures) {
            out_log << fut.get().rdbuf();
        }
        
        out_log.close();

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

        std::cout << "\n--- Decompression Summary ---" << std::endl;
        std::cout << "Decompression complete." << std::endl;
        std::cout << "Total execution time: " << duration.count() << " ms" << std::endl;
        std::cout << "Output file is located at: " << output_file << std::endl;
        
        try {
            uintmax_t total_compressed_size = 0;
            for (const auto& file : chunk_files) {
                total_compressed_size += std::filesystem::file_size(file);
            }
            uintmax_t decompressed_size = std::filesystem::file_size(output_file);

            double compressed_mb = total_compressed_size / (1024.0 * 1024.0);
            double decompressed_mb = decompressed_size / (1024.0 * 1024.0);
            double time_s = duration.count() / 1000.0;
            double speed_mbps = (time_s > 0) ? (decompressed_mb / time_s) : 0.0;

            std::cout << "Total compressed size: " << std::fixed << std::setprecision(2) << compressed_mb << " MB\n";
            std::cout << "Decompressed size: " << std::fixed << std::setprecision(2) << decompressed_mb << " MB\n";
            std::cout << "Decompression speed: " << std::fixed << std::setprecision(2) << speed_mbps << " MB/s\n";
            
            std::string logname = compressed_dir.filename().string();
            log_decompression_result(logname, num_threads, compressed_mb, decompressed_mb, duration.count(), speed_mbps);

        } catch (const std::filesystem::filesystem_error& e) {
            std::cerr << "Error getting file sizes for final report: " << e.what() << std::endl;
        }

    } catch (const std::exception& e) {
        std::cerr << "An error occurred: " << e.what() << std::endl;
        std::filesystem::remove_all(temp_base_path);
        return 1;
    }
    
    std::filesystem::remove_all(temp_base_path);
    return 0;
}