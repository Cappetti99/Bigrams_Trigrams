
//
// Created by Lorenzo Cappetti on 21/11/25.
// Optimized with Sharded Map & Arena Allocation
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
// ARENA ALLOCATOR
// Simple pointer-bump allocator for fast string storage.
//═══════════════════════════════════════════════════════════════
class Arena {
private:
    struct Block {
        std::unique_ptr<char[]> data;
        size_t size;
        size_t used;
    };
    std::vector<Block> blocks;

public:
    Arena() {
        // Pre-allocate first block
        allocate_block(ARENA_BLOCK_SIZE);
    }

    // Disable copy/move to prevent accidental double-frees or pointer invalidation
    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;

    void allocate_block(size_t size) {
        blocks.push_back({std::make_unique<char[]>(size), size, 0});
    }

    // Allocates memory for a string and returns a string_view to it
    std::string_view allocate(std::string_view s) {
        if (blocks.back().used + s.size() > blocks.back().size) {
            // Need a new block. Make it at least large enough for s, or default size
            size_t next_size = std::max(ARENA_BLOCK_SIZE, s.size());
            allocate_block(next_size);
        }

        Block& current = blocks.back();
        char* dest = current.data.get() + current.used;
        std::memcpy(dest, s.data(), s.size());
        current.used += s.size();

        return std::string_view(dest, s.size());
    }

    size_t total_memory() const {
        size_t total = 0;
        for(const auto& b : blocks) total += b.size;
        return total;
    }
};

//═══════════════════════════════════════════════════════════════
// SHARDED HASH MAP
// A thread-safe hash map split into shards to minimize locking.
// Each shard has its own Mutex, Map, and Arena.
//═══════════════════════════════════════════════════════════════
class ShardedMap {
private:
    struct alignas(64) Shard { // Align to cache line to prevent false sharing
        std::mutex mtx;
        std::unordered_map<std::string_view, size_t> map;
        Arena arena;
    };

    std::vector<Shard> shards;

public:
    ShardedMap() : shards(NUM_SHARDS) {}

    void insert_or_increment(std::string_view key) {
        // Simple hash to pick a shard
        size_t h = std::hash<std::string_view>{}(key);
        size_t shard_idx = h % NUM_SHARDS;
        Shard& shard = shards[shard_idx];

        std::lock_guard<std::mutex> lock(shard.mtx);

        auto it = shard.map.find(key);
        if (it != shard.map.end()) {
            it->second++;
        } else {
            // Store the string in the shard's arena so it persists
            std::string_view stored_key = shard.arena.allocate(key);
            shard.map[stored_key] = 1;
        }
    }

    // Merges all shards into a single vector for sorting/output
    std::vector<std::pair<std::string, size_t>> get_all_sorted() const {
        std::vector<std::pair<std::string, size_t>> result;
        size_t total_size = 0;
        for (const auto& shard : shards) total_size += shard.map.size();
        result.reserve(total_size);

        for (const auto& shard : shards) {
            // No lock needed here if we are guaranteed to be single-threaded at this point
            for (const auto& [key, count] : shard.map) {
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
        for (const auto& shard : shards) count += shard.map.size();
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
// TOKENIZER
//═══════════════════════════════════════════════════════════════
class Tokenizer {
private:
    struct CharInfo {
        unsigned char lower;
        uint8_t flags;
    };

    static const CharInfo* get_char_table() {
        static CharInfo table[256];
        static bool initialized = false;

        if (!initialized) {
            for (int i = 0; i < 256; i++) {
                table[i].lower = (i >= 'A' && i <= 'Z') ? i + 32 : i;
                table[i].flags = 0;
                if ((i >= 'a' && i <= 'z') || (i >= 'A' && i <= 'Z')) table[i].flags |= 1;
                if (i == ' ' || i == '\t' || i == '\n' || i == '\r') table[i].flags |= 2;
                if ((i >= 33 && i <= 47) || (i >= 58 && i <= 64) ||
                    (i >= 91 && i <= 96) || (i >= 123 && i <= 126)) table[i].flags |= 4;
                if (i >= '0' && i <= '9') table[i].flags |= 8;
            }
            initialized = true;
        }
        return table;
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
        const CharInfo* table = get_char_table();
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

            const auto& info = table[c];

            if (info.flags & 8) continue;

            if (remove_punct && (info.flags & 4)) {
                *write++ = ' ';
            } else if (info.flags & 1) {
                *write++ = info.lower;
            } else if (info.flags & 2) {
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
        const ShardedMap& map,
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
// OPTIMIZED OPENMP PROCESSOR
//═══════════════════════════════════════════════════════════════
class OptimizedOpenMPProcessor {
public:
    static void process_text_sharded(
        std::string& text,
        ShardedMap& word_bigrams,
        ShardedMap& word_trigrams,
        ShardedMap& char_bigrams,
        ShardedMap& char_trigrams,
        std::vector<std::string_view>& words_buffer,
        std::vector<char>& chars_buffer
    ) {
        Tokenizer::normalize_inplace(text, true);

        words_buffer.clear();
        Tokenizer::tokenize_words(text, words_buffer);

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
        Tokenizer::tokenize_chars(text, chars_buffer);

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
        ShardedMap& word_bigrams,
        ShardedMap& word_trigrams,
        ShardedMap& char_bigrams,
        ShardedMap& char_trigrams,
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
// MAIN
//═══════════════════════════════════════════════════════════════
int main()
{
    std::string folder_path = "/home/lollo/CLionProjects/Bigrams_Trigrams/book_gutenberg/book_gutenberg";

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

    struct RunResult { double wall; double cpu; bool warmup; };
    std::vector<RunResult> run_times;
    run_times.reserve(NUM_RUNS);

    // Use pointers to recreate maps for each run to ensure clean state
    std::unique_ptr<ShardedMap> word_bigrams, word_trigrams, char_bigrams, char_trigrams;

    for (int run = 0; run < NUM_RUNS; ++run) {
        word_bigrams = std::make_unique<ShardedMap>();
        word_trigrams = std::make_unique<ShardedMap>();
        char_bigrams = std::make_unique<ShardedMap>();
        char_trigrams = std::make_unique<ShardedMap>();

        auto start_time = std::chrono::high_resolution_clock::now();
        std::clock_t start_cpu = std::clock();

        OptimizedOpenMPProcessor::process_parallel_sharded(
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

        run_times.push_back(RunResult{elapsed.count(), cpu_seconds, run < WARMUP_RUNS});

        std::cout << "[Run " << (run + 1) << "/" << NUM_RUNS << "] "
                  << std::fixed << std::setprecision(2) << elapsed.count() << "s\n";
    }

    std::vector<double> measured_times;
    std::vector<double> measured_cpu;
    for (const auto& r : run_times) {
        if (!r.warmup) {
            measured_times.push_back(r.wall);
            measured_cpu.push_back(r.cpu);
        }
    }

    double mean = 0.0, min_time = measured_times[0], max_time = measured_times[0];
    for (double t : measured_times) {
        mean += t;
        min_time = std::min(min_time, t);
        max_time = std::max(max_time, t);
    }
    mean /= measured_times.size();

    double stddev = 0.0;
    for (double t : measured_times) {
        stddev += (t - mean) * (t - mean);
    }
    stddev = std::sqrt(stddev / measured_times.size());
    double cv = (stddev / mean) * 100.0;

    double mean_cpu = 0.0, min_cpu = measured_cpu[0], max_cpu = measured_cpu[0];
    for (double t : measured_cpu) {
        mean_cpu += t;
        min_cpu = std::min(min_cpu, t);
        max_cpu = std::max(max_cpu, t);
    }
    mean_cpu /= measured_cpu.size();

    double stddev_cpu = 0.0;
    for (double t : measured_cpu) {
        stddev_cpu += (t - mean_cpu) * (t - mean_cpu);
    }
    stddev_cpu = std::sqrt(stddev_cpu / measured_cpu.size());
    double cv_cpu = (stddev_cpu / mean_cpu) * 100.0;

    std::string output_dir = "results/parallel";
    ensure_directory_exists(output_dir);

    CSVSaver::save_ngrams(*word_bigrams, output_dir + "/word_bigrams.csv", "Word Bigrams");
    CSVSaver::save_ngrams(*word_trigrams, output_dir + "/word_trigrams.csv", "Word Trigrams");
    CSVSaver::save_ngrams(*char_bigrams, output_dir + "/char_bigrams.csv", "Char Bigrams");
    CSVSaver::save_ngrams(*char_trigrams, output_dir + "/char_trigrams.csv", "Char Trigrams");

    // Save performance statistics + n-gram info in one file
    std::ofstream stats_file(output_dir + "/performance_stats.txt");
    if (stats_file) {
        stats_file << "╔═══════════════════════════════════════════════════════╗\n";
        stats_file << "║         PERFORMANCE SUMMARY (" << MEASURED_RUNS << " runs)              ║\n";
        stats_file << "╠═══════════════════════════════════════════════════════╣\n";
        stats_file << "║ THREADS USED:  " << std::setw(38) << num_threads << " ║\n";
        stats_file << "╠═══════════════════════════════════════════════════════╣\n";
        stats_file << "║ WALL-CLOCK TIME                                       ║\n";
        stats_file << "║   Mean:         " << std::setw(11) << std::fixed << std::setprecision(3) << mean << " s                        ║\n";
        stats_file << "║   Minimum:      " << std::setw(11) << std::fixed << std::setprecision(3) << min_time << " s                        ║\n";
        stats_file << "║   Maximum:      " << std::setw(11) << std::fixed << std::setprecision(3) << max_time << " s                        ║\n";
        stats_file << "║   Std Deviation:" << std::setw(11) << std::fixed << std::setprecision(3) << stddev << " s                        ║\n";
        stats_file << "║   Coeff. Variation:" << std::setw(8) << std::fixed << std::setprecision(2) << cv << " %                         ║\n";
        stats_file << "║                                                       ║\n";
        stats_file << "║ CPU TIME                                              ║\n";
        stats_file << "║   Mean:         " << std::setw(11) << std::fixed << std::setprecision(3) << mean_cpu << " s                        ║\n";
        stats_file << "║   Minimum:      " << std::setw(11) << std::fixed << std::setprecision(3) << min_cpu << " s                        ║\n";
        stats_file << "║   Maximum:      " << std::setw(11) << std::fixed << std::setprecision(3) << max_cpu << " s                        ║\n";
        stats_file << "║   Std Deviation:" << std::setw(11) << std::fixed << std::setprecision(3) << stddev_cpu << " s                        ║\n";
        stats_file << "║   Coeff. Variation:" << std::setw(8) << std::fixed << std::setprecision(2) << cv_cpu << " %                         ║\n";
        stats_file << "╚═══════════════════════════════════════════════════════╝\n";
        stats_file << "\n";
        stats_file << "N-GRAM STATISTICS:\n";
        stats_file << "  Word Bigrams:  " << word_bigrams->total_unique() << " unique\n";
        stats_file << "  Word Trigrams: " << word_trigrams->total_unique() << " unique\n";
        stats_file << "  Char Bigrams:  " << char_bigrams->total_unique() << " unique\n";
        stats_file << "  Char Trigrams: " << char_trigrams->total_unique() << " unique\n";
        stats_file.close();
        std::cout << "\nPerformance statistics saved to " << output_dir << "/performance_stats.txt\n";
    }

    return 0;
}