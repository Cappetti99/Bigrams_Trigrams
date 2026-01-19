//
// Created by Lorenzo Cappetti on 21/11/25.
//
// Parallel N-gram Analysis - SoA (Structure of Arrays) Version
// SoA layout: separate arrays for each field vs AoS (array of structs).
// E.g., data[], sizes[], used[] instead of vector<{data, size, used}>.
// Better cache when accessing one field, but more complex indexing.
//

#include <iostream>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <filesystem>
#include <chrono>
#include <omp.h>
#include <iomanip>
#include <numeric>
#include <cmath>
#include <ctime>
#include <cstring>
#include <string_view>
#include <memory>
#include <mutex>
#include <array>

namespace fs = std::filesystem;

//═══════════════════════════════════════════════════════════════
// COMPILE-TIME CONSTANTS
//═══════════════════════════════════════════════════════════════
constexpr size_t ARENA_BLOCK_SIZE = 4 * 1024 * 1024; // 4MB blocks
constexpr size_t NUM_SHARDS = 1024;                  // Number of shards for the hash map
constexpr size_t WORD_BUFFER_SIZE = 20000;
constexpr size_t CHAR_BUFFER_SIZE = 100000;

//═══════════════════════════════════════════════════════════════
// UTILITY FUNCTIONS
//═══════════════════════════════════════════════════════════════
static inline void ensure_directory_exists(const std::string& dir) {
    if (!fs::exists(dir)) {
        fs::create_directories(dir);
    }
}

//═══════════════════════════════════════════════════════════════
// ARENA ALLOCATOR - SoA VERSION
// Separate arrays: data[], sizes[], used[] instead of vector<Block>.
// Good cache when checking sizes, more pointer chasing for full block access.
//═══════════════════════════════════════════════════════════════
class ArenaSoA {
private:
    // SoA: separate array per field (vs AoS: vector<struct>)
    std::vector<std::unique_ptr<char[]>> block_data;
    std::vector<size_t> block_sizes;
    std::vector<size_t> block_used;

public:
    ArenaSoA() {
        allocate_block(ARENA_BLOCK_SIZE);
    }

    ArenaSoA(const ArenaSoA&) = delete;
    ArenaSoA& operator=(const ArenaSoA&) = delete;

    void allocate_block(size_t size) {
        block_data.push_back(std::make_unique<char[]>(size));
        block_sizes.push_back(size);
        block_used.push_back(0);
    }

    std::string_view allocate(std::string_view s) {
        size_t last_idx = block_data.size() - 1;
        
        // Check size in separate array (cache-friendly when iterating many blocks)
        if (block_used[last_idx] + s.size() > block_sizes[last_idx]) {
            size_t next_size = std::max(ARENA_BLOCK_SIZE, s.size());
            allocate_block(next_size);
            last_idx = block_data.size() - 1;
        }

        // Three separate array accesses
        char* dest = block_data[last_idx].get() + block_used[last_idx];
        std::memcpy(dest, s.data(), s.size());
        block_used[last_idx] += s.size();

        return std::string_view(dest, s.size());
    }

    size_t total_memory() const {
        size_t total = 0;
        for (size_t s : block_sizes) total += s;
        return total;
    }
};

//═══════════════════════════════════════════════════════════════
// SHARDED HASH MAP - SoA VERSION
// Separate vectors: mutexes[], maps[], arenas[] (vs struct with all three).
// Mutexes still cache-aligned to avoid false sharing.
//═══════════════════════════════════════════════════════════════
class ShardedMapSoA {
private:
    // Separate arrays, mutexes cache-aligned for false sharing prevention
    alignas(64) std::vector<std::mutex> shard_mutexes;
    std::vector<std::unordered_map<std::string_view, size_t>> shard_maps;
    std::vector<std::unique_ptr<ArenaSoA>> shard_arenas;

public:
    ShardedMapSoA() : 
        shard_mutexes(NUM_SHARDS),
        shard_maps(NUM_SHARDS) 
    {
        shard_arenas.reserve(NUM_SHARDS);
        for (size_t i = 0; i < NUM_SHARDS; ++i) {
            shard_arenas.push_back(std::make_unique<ArenaSoA>());
        }
    }

    void insert_or_increment(std::string_view key) {
        size_t h = std::hash<std::string_view>{}(key);
        size_t shard_idx = h % NUM_SHARDS;

        // Same index across different arrays
        std::lock_guard<std::mutex> lock(shard_mutexes[shard_idx]);

        auto it = shard_maps[shard_idx].find(key);
        if (it != shard_maps[shard_idx].end()) {
            it->second++;
        } else {
            std::string_view stored_key = shard_arenas[shard_idx]->allocate(key);
            shard_maps[shard_idx][stored_key] = 1;
        }
    }

    std::vector<std::pair<std::string, size_t>> get_all_sorted() const {
        std::vector<std::pair<std::string, size_t>> result;
        size_t total_size = 0;
        for (const auto& map : shard_maps) total_size += map.size();
        result.reserve(total_size);

        for (const auto& map : shard_maps) {
            for (const auto& [key, count] : map) {
                result.emplace_back(std::string(key), count);
            }
        }

        std::sort(result.begin(), result.end(),
                  [](const auto& a, const auto& b) {
                      return a.second > b.second;
                  });
        return result;
    }

    size_t total_unique() const {
        size_t count = 0;
        for (const auto& map : shard_maps) count += map.size();
        return count;
    }
};

//═══════════════════════════════════════════════════════════════
// OPTIMIZED TEXT CLEANER
//═══════════════════════════════════════════════════════════════
class TextCleaner {
private:
    static constexpr std::string_view START_MARKER = "*** START OF";
    static constexpr std::string_view END_MARKER = "*** END OF";

public:
    static inline void clean_text_inplace(std::string& text) {
        size_t start_pos = 0;
        size_t end_pos = text.size();

        const char* found = static_cast<const char*>(
            memmem(text.data(), text.size(), START_MARKER.data(), START_MARKER.size())
        );

        if (found) {
            size_t offset = found - text.data();
            const char* marker_end = static_cast<const char*>(
                memmem(found + START_MARKER.size(),
                       text.size() - offset - START_MARKER.size(),
                       "***", 3)
            );
            if (marker_end) {
                start_pos = (marker_end - text.data()) + 3;
            }
        }

        if (start_pos < text.size()) {
            const char* end_found = static_cast<const char*>(
                memmem(text.data() + start_pos,
                       text.size() - start_pos,
                       END_MARKER.data(),
                       END_MARKER.size())
            );
            if (end_found) {
                end_pos = end_found - text.data();
            }
        }

        if (start_pos > 0 || end_pos < text.size()) {
            if (start_pos > 0) {
                memmove(text.data(), text.data() + start_pos, end_pos - start_pos);
            }
            text.resize(end_pos - start_pos);
        }
    }
};

//═══════════════════════════════════════════════════════════════
// TOKENIZER - SoA VERSION
// Separate arrays: lower[256] and flags[256] (vs array of struct).
// Better cache when only checking flags or only converting to lowercase.
//═══════════════════════════════════════════════════════════════
class TokenizerSoA {
private:
    // Separate arrays, cache-aligned (read-only after init)
    struct CharTablesSoA {
        alignas(64) unsigned char lower[256];
        alignas(64) uint8_t flags[256];
        bool initialized = false;
    };

    static CharTablesSoA& get_char_tables() {
        static CharTablesSoA tables;
        
        if (!tables.initialized) {
            for (int i = 0; i < 256; i++) {
                tables.lower[i] = (i >= 'A' && i <= 'Z') ? i + 32 : i;
                tables.flags[i] = 0;
                if ((i >= 'a' && i <= 'z') || (i >= 'A' && i <= 'Z')) tables.flags[i] |= 1;
                if (i == ' ' || i == '\t' || i == '\n' || i == '\r') tables.flags[i] |= 2;
                if ((i >= 33 && i <= 47) || (i >= 58 && i <= 64) ||
                    (i >= 91 && i <= 96) || (i >= 123 && i <= 126)) tables.flags[i] |= 4;
                if (i >= '0' && i <= '9') tables.flags[i] |= 8;
            }
            tables.initialized = true;
        }
        return tables;
    }

    static inline std::string_view process_utf8_char(const unsigned char* bytes, size_t& skip) {
        skip = 0;
        if ((bytes[0] & 0xE0) == 0xC0 && bytes[1]) {
            skip = 2;
            unsigned char first = bytes[0];
            unsigned char second = bytes[1];

            if (first == 0xC3) {
                if ((second >= 0x80 && second <= 0x85) || (second >= 0xA0 && second <= 0xA5)) return "a";
                if ((second >= 0x88 && second <= 0x8B) || (second >= 0xA8 && second <= 0xAB)) return "e";
                if ((second >= 0x8C && second <= 0x8F) || (second >= 0xAC && second <= 0xAF)) return "i";
                if ((second >= 0x92 && second <= 0x96) || (second >= 0xB2 && second <= 0xB6)) return "o";
                if ((second >= 0x99 && second <= 0x9C) || (second >= 0xB9 && second <= 0xBC)) return "u";
                if (second == 0x91 || second == 0xB1) return "n";
                if (second == 0x87 || second == 0xA7) return "c";
            }
            return std::string_view();
        }

        if (bytes[0] >= 0x80) {
            skip = 1;
            while (skip < 4 && bytes[skip] && (bytes[skip] & 0xC0) == 0x80) skip++;
            return std::string_view();
        }
        return std::string_view();
    }

public:
    static void normalize_inplace(std::string& text, bool remove_punct = false) {
        const auto& tables = get_char_tables();
        // Separate pointers for each array
        const unsigned char* lower_table = tables.lower;
        const uint8_t* flags_table = tables.flags;
        
        char* write = &text[0];
        const char* read = text.data();
        const size_t size = text.size();

        for (size_t i = 0; i < size; ++i) {
            unsigned char c = static_cast<unsigned char>(read[i]);

            if (c >= 0x80) {
                size_t skip;
                auto replacement = process_utf8_char(
                    reinterpret_cast<const unsigned char*>(&read[i]), skip);
                if (skip > 0) {
                    for (char ch : replacement) {
                        *write++ = ch;
                    }
                    i += skip - 1;
                    continue;
                }
            }

            // Check flags first, only access lower if needed (better cache)
            uint8_t flags = flags_table[c];

            if (flags & 8) continue;

            if (remove_punct && (flags & 4)) {
                *write++ = ' ';
            } else if (flags & 1) {
                *write++ = lower_table[c];  // Separate array access
            } else if (flags & 2) {
                *write++ = ' ';
            }
        }

        text.resize(write - &text[0]);
    }

    static inline void tokenize_words(const std::string& text, std::vector<std::string_view>& tokens) {
        tokens.clear();

        const char* start = text.data();
        const char* end = start + text.size();
        const char* word_start = nullptr;

        for (const char* p = start; p <= end; ++p) {
            bool is_space = (p == end || *p == ' ' || *p == '\t' || *p == '\n');

            if (!is_space && !word_start) {
                word_start = p;
            } else if (is_space && word_start) {
                tokens.emplace_back(word_start, p - word_start);
                word_start = nullptr;
            }
        }
    }

    static inline void tokenize_chars(const std::string& text, std::vector<char>& chars) {
        chars.clear();
        chars.reserve(text.size());

        const char* data = text.data();
        const size_t size = text.size();

        for (size_t i = 0; i < size; ++i) {
            char c = data[i];
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
                chars.push_back(c);
            }
        }
    }
};

//═══════════════════════════════════════════════════════════════
// CSV SAVER
//═══════════════════════════════════════════════════════════════
class CSVSaver {
public:
    static void save_ngrams(
        const ShardedMapSoA& map,
        const std::string& filename,
        const std::string& label
    ) {
        auto all_ngrams = map.get_all_sorted();

        std::ofstream out(filename);
        if (!out) {
            std::cerr << "Error opening file: " << filename << "\n";
            return;
        }

        char buffer[65536];
        out.rdbuf()->pubsetbuf(buffer, sizeof(buffer));

        out << "ngram,frequency\n";

        for (const auto& [ngram, freq] : all_ngrams) {
            out << "\"" << ngram << "\"," << freq << "\n";
        }

        out.close();
        std::cout << label << ": " << all_ngrams.size()
                  << " n-grams saved to " << filename << "\n";
    }
};

//═══════════════════════════════════════════════════════════════
// BENCHMARK RESULTS - SoA VERSION
// Separate arrays: wall_times[], cpu_times[], is_warmup[].
// Good for computing stats on just one metric (tight loop, better cache).
//═══════════════════════════════════════════════════════════════
class BenchmarkResultsSoA {
public:
    // Each metric in its own contiguous array
    std::vector<double> wall_times;
    std::vector<double> cpu_times;
    std::vector<bool> is_warmup;
    
    void reserve(size_t n) {
        wall_times.reserve(n);
        cpu_times.reserve(n);
        is_warmup.reserve(n);
    }
    
    void add_result(double wall, double cpu, bool warmup) {
        wall_times.push_back(wall);
        cpu_times.push_back(cpu);
        is_warmup.push_back(warmup);
    }
    
    size_t size() const { return wall_times.size(); }
    
    // Iterate only wall_times[] (tight loop, good cache)
    void compute_wall_stats(double& mean, double& min_val, double& max_val, 
                            double& stddev, double& cv) const {
        std::vector<double> measured;
        for (size_t i = 0; i < wall_times.size(); ++i) {
            if (!is_warmup[i]) {
                measured.push_back(wall_times[i]);
            }
        }
        
        if (measured.empty()) return;
        
        mean = 0.0;
        min_val = measured[0];
        max_val = measured[0];
        
        for (double t : measured) {
            mean += t;
            min_val = std::min(min_val, t);
            max_val = std::max(max_val, t);
        }
        mean /= measured.size();
        
        stddev = 0.0;
        for (double t : measured) {
            stddev += (t - mean) * (t - mean);
        }
        stddev = std::sqrt(stddev / measured.size());
        cv = (stddev / mean) * 100.0;
    }
    
    void compute_cpu_stats(double& mean, double& min_val, double& max_val,
                           double& stddev, double& cv) const {
        std::vector<double> measured;
        for (size_t i = 0; i < cpu_times.size(); ++i) {
            if (!is_warmup[i]) {
                measured.push_back(cpu_times[i]);
            }
        }
        
        if (measured.empty()) return;
        
        mean = 0.0;
        min_val = measured[0];
        max_val = measured[0];
        
        for (double t : measured) {
            mean += t;
            min_val = std::min(min_val, t);
            max_val = std::max(max_val, t);
        }
        mean /= measured.size();
        
        stddev = 0.0;
        for (double t : measured) {
            stddev += (t - mean) * (t - mean);
        }
        stddev = std::sqrt(stddev / measured.size());
        cv = (stddev / mean) * 100.0;
    }
};

//═══════════════════════════════════════════════════════════════
// OPTIMIZED OPENMP PROCESSOR - SoA VERSION
//═══════════════════════════════════════════════════════════════
class OptimizedOpenMPProcessorSoA {
public:
    static void process_text_sharded(
        std::string& text,
        ShardedMapSoA& word_bigrams,
        ShardedMapSoA& word_trigrams,
        ShardedMapSoA& char_bigrams,
        ShardedMapSoA& char_trigrams,
        std::vector<std::string_view>& words_buffer,
        std::vector<char>& chars_buffer
    ) {
        TokenizerSoA::normalize_inplace(text, true);

        words_buffer.clear();
        TokenizerSoA::tokenize_words(text, words_buffer);

        static thread_local std::string key_buffer;
        key_buffer.reserve(256);

        const size_t word_count = words_buffer.size();

        for (size_t i = 0; i + 1 < word_count; ++i) {
            key_buffer.clear();
            key_buffer.append(words_buffer[i]);
            key_buffer.push_back(' ');
            key_buffer.append(words_buffer[i + 1]);
            word_bigrams.insert_or_increment(key_buffer);
        }

        for (size_t i = 0; i + 2 < word_count; ++i) {
            key_buffer.clear();
            key_buffer.append(words_buffer[i]);
            key_buffer.push_back(' ');
            key_buffer.append(words_buffer[i + 1]);
            key_buffer.push_back(' ');
            key_buffer.append(words_buffer[i + 2]);
            word_trigrams.insert_or_increment(key_buffer);
        }

        chars_buffer.clear();
        TokenizerSoA::tokenize_chars(text, chars_buffer);

        const size_t char_count = chars_buffer.size();
        char char_key[6];

        for (size_t i = 0; i + 1 < char_count; ++i) {
            char_key[0] = chars_buffer[i];
            char_key[1] = ' ';
            char_key[2] = chars_buffer[i + 1];
            char_bigrams.insert_or_increment(std::string_view(char_key, 3));
        }

        for (size_t i = 0; i + 2 < char_count; ++i) {
            char_key[0] = chars_buffer[i];
            char_key[1] = ' ';
            char_key[2] = chars_buffer[i + 1];
            char_key[3] = ' ';
            char_key[4] = chars_buffer[i + 2];
            char_trigrams.insert_or_increment(std::string_view(char_key, 5));
        }
    }

    static void process_parallel_sharded(
        const std::vector<std::string>& book_files,
        ShardedMapSoA& word_bigrams,
        ShardedMapSoA& word_trigrams,
        ShardedMapSoA& char_bigrams,
        ShardedMapSoA& char_trigrams,
        int num_threads = 0
    ) {
        if (num_threads == 0) num_threads = omp_get_max_threads();
        omp_set_num_threads(num_threads);

        int total_books = book_files.size();

        #pragma omp parallel default(none) shared(book_files, total_books, word_bigrams, word_trigrams, char_bigrams, char_trigrams)
        {
            std::vector<std::string_view> words_buf;
            std::vector<char> chars_buf;
            words_buf.reserve(WORD_BUFFER_SIZE);
            chars_buf.reserve(CHAR_BUFFER_SIZE);

            #pragma omp for schedule(dynamic)
            for (int i = 0; i < total_books; ++i) {
                const auto& filepath = book_files[i];

                std::ifstream file(filepath, std::ios::binary | std::ios::ate);
                if (!file) continue;

                std::streamsize size = file.tellg();
                file.seekg(0, std::ios::beg);
                std::string text(size, '\0');
                if (!file.read(&text[0], size)) continue;

                TextCleaner::clean_text_inplace(text);

                process_text_sharded(text, word_bigrams, word_trigrams, char_bigrams, char_trigrams,
                                     words_buf, chars_buf);
            }
        }
    }
};

//═══════════════════════════════════════════════════════════════
// MAIN - SoA VERSION
// Uses SoA throughout. Compare with parallel.cpp (AoS) for performance.
// Expected: similar or better when accessing one field, worse for all fields.
//═══════════════════════════════════════════════════════════════
int main()
{
    std::cout << "╔═══════════════════════════════════════════════════════╗\n";
    std::cout << "║     PARALLEL N-GRAM ANALYZER - SoA VERSION           ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════╝\n\n";

    std::string folder_path = "/Users/lorenzocappetti/CLionProjects/Bigrams_Trigrams/book_gutenberg";

    if (!fs::exists(folder_path)) {
        std::cerr << "Folder not found: " << folder_path << "\n";
        return 1;
    }

    std::vector<std::string> book_files;
    book_files.reserve(2000);

    for (const auto& entry : fs::directory_iterator(folder_path)) {
        if (entry.path().extension() == ".txt") {
            book_files.push_back(entry.path().string());
        }
    }

    if (book_files.empty()) {
        std::cerr << "No .txt files found!\n";
        return 1;
    }

    int max_threads = omp_get_max_threads();
    const int MAX_VIRTUAL_THREADS = 1000;
    int num_threads;

    std::cout << "Threads available: " << max_threads << "\n";
    std::cout << "Enter threads (1-" << MAX_VIRTUAL_THREADS << "): ";
    std::cout.flush();

    while (true) {
        std::cin >> num_threads;

        if (std::cin.fail()) {
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            std::cout << "Invalid input. Enter a number (1-" << MAX_VIRTUAL_THREADS << "): ";
            continue;
        }

        if (num_threads < 1 || num_threads > MAX_VIRTUAL_THREADS) {
            std::cout << "Out of range. Enter a value between 1 and " << MAX_VIRTUAL_THREADS << ": ";
            continue;
        }

        break;
    }

    const int WARMUP_RUNS = 2;
    const int MEASURED_RUNS = 10;
    const int NUM_RUNS = WARMUP_RUNS + MEASURED_RUNS;

    // SoA for benchmark data
    BenchmarkResultsSoA results;
    results.reserve(NUM_RUNS);

    std::unique_ptr<ShardedMapSoA> word_bigrams, word_trigrams, char_bigrams, char_trigrams;

    for (int run = 0; run < NUM_RUNS; ++run) {
        word_bigrams = std::make_unique<ShardedMapSoA>();
        word_trigrams = std::make_unique<ShardedMapSoA>();
        char_bigrams = std::make_unique<ShardedMapSoA>();
        char_trigrams = std::make_unique<ShardedMapSoA>();

        auto start_time = std::chrono::high_resolution_clock::now();
        std::clock_t start_cpu = std::clock();

        OptimizedOpenMPProcessorSoA::process_parallel_sharded(
            book_files,
            *word_bigrams,
            *word_trigrams,
            *char_bigrams,
            *char_trigrams,
            num_threads
        );

        auto end_time = std::chrono::high_resolution_clock::now();
        std::clock_t end_cpu = std::clock();
        std::chrono::duration<double> elapsed = end_time - start_time;
        double cpu_seconds = double(end_cpu - start_cpu) / double(CLOCKS_PER_SEC);

        // Store in separate arrays
        results.add_result(elapsed.count(), cpu_seconds, run < WARMUP_RUNS);

        std::cout << "[Run " << (run + 1) << "/" << NUM_RUNS << "] "
                  << std::fixed << std::setprecision(2) << elapsed.count() << "s"
                  << (run < WARMUP_RUNS ? " (warmup)" : "") << "\n";
    }

    // Compute stats using SoA (iterates only needed arrays)
    double mean, min_time, max_time, stddev, cv;
    double mean_cpu, min_cpu, max_cpu, stddev_cpu, cv_cpu;
    
    results.compute_wall_stats(mean, min_time, max_time, stddev, cv);
    results.compute_cpu_stats(mean_cpu, min_cpu, max_cpu, stddev_cpu, cv_cpu);

    std::string output_dir = "results/parallel_soa";
    ensure_directory_exists(output_dir);

    CSVSaver::save_ngrams(*word_bigrams, output_dir + "/word_bigrams.csv", "Word Bigrams");
    CSVSaver::save_ngrams(*word_trigrams, output_dir + "/word_trigrams.csv", "Word Trigrams");
    CSVSaver::save_ngrams(*char_bigrams, output_dir + "/char_bigrams.csv", "Char Bigrams");
    CSVSaver::save_ngrams(*char_trigrams, output_dir + "/char_trigrams.csv", "Char Trigrams");

    std::ofstream stats_file(output_dir + "/performance_stats.txt");
    if (stats_file) {
        stats_file << "PERFORMANCE SUMMARY - SoA VERSION (" << MEASURED_RUNS << " runs)\n";
        stats_file << "Threads: " << num_threads << "\n\n";
        
        stats_file << "WALL-CLOCK TIME:\n";
        stats_file << "  Mean: " << std::fixed << std::setprecision(3) << mean << " s\n";
        stats_file << "  Min:  " << min_time << " s\n";
        stats_file << "  Max:  " << max_time << " s\n";
        stats_file << "  Std:  " << stddev << " s\n";
        stats_file << "  CV:   " << std::setprecision(2) << cv << " %\n\n";
        
        stats_file << "CPU TIME:\n";
        stats_file << "  Mean: " << std::setprecision(3) << mean_cpu << " s\n";
        stats_file << "  Min:  " << min_cpu << " s\n";
        stats_file << "  Max:  " << max_cpu << " s\n";
        stats_file << "  Std:  " << stddev_cpu << " s\n";
        stats_file << "  CV:   " << std::setprecision(2) << cv_cpu << " %\n\n";
        
        stats_file << "N-GRAM STATISTICS:\n";
        stats_file << "  Word Bigrams:  " << word_bigrams->total_unique() << "\n";
        stats_file << "  Word Trigrams: " << word_trigrams->total_unique() << "\n";
        stats_file << "  Char Bigrams:  " << char_bigrams->total_unique() << "\n";
        stats_file << "  Char Trigrams: " << char_trigrams->total_unique() << "\n";
        stats_file << "\n";
        stats_file << "DATA LAYOUT: Structure of Arrays (SoA)\n";
        stats_file << "Compare with parallel.cpp (AoS) to see layout impact on performance.\n";
        stats_file << "Compare with parallel.cpp (AoS) to see layout impact on performance.\n";
        stats_file.close();
        std::cout << "\nPerformance statistics saved to " << output_dir << "/performance_stats.txt\n";
    }

    // Stampa sommario a console
    std::cout << "\n╔═══════════════════════════════════════════════════════╗\n";
    std::cout << "║                    SUMMARY                            ║\n";
    std::cout << "╠═══════════════════════════════════════════════════════╣\n";
    std::cout << "║ Mean Wall Time: " << std::setw(10) << std::fixed << std::setprecision(3) << mean << " s                       ║\n";
    std::cout << "║ Mean CPU Time:  " << std::setw(10) << std::fixed << std::setprecision(3) << mean_cpu << " s                       ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════╝\n";

    return 0;
}
