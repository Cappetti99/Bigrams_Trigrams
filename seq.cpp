//
// Created by Lorenzo Cappetti on 21/11/25.
//
// Sequential N-gram Analysis Program
// Analyzes Project Gutenberg books to count word/char bigrams and trigrams.
// Main optimizations: string interning, cache-aligned structures, in-place operations,
// and a two-phase approach (hash map accumulation → sorted array queries).
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
#include <iomanip>
#include <numeric>
#include <cmath>
#include <ctime>
#include <cstring>
#include <string_view>
#include <memory>

namespace fs = std::filesystem;

//═══════════════════════════════════════════════════════════════
// COMPILE-TIME CONSTANTS
// Pre-allocated sizes tuned for Gutenberg books to avoid reallocations
//═══════════════════════════════════════════════════════════════
constexpr size_t ARENA_INITIAL_SIZE = 20'000'000;  // String pool arena size
constexpr size_t WORD_BUFFER_SIZE = 20000;
constexpr size_t CHAR_BUFFER_SIZE = 100000;
constexpr size_t WB_RESERVE = 150000;
constexpr size_t WT_RESERVE = 300000;
constexpr size_t CB_RESERVE = 75000;
constexpr size_t CT_RESERVE = 150000;

//═══════════════════════════════════════════════════════════════
// UTILITY FUNCTIONS
//═══════════════════════════════════════════════════════════════
static inline void ensure_directory_exists(const std::string& dir) {
    if (!fs::exists(dir)) {
        fs::create_directories(dir);
    }
}

//═══════════════════════════════════════════════════════════════
// TEXT CLEANER
// Strips Gutenberg headers/footers using memmem() (faster than string::find).
// Everything happens in-place to avoid copies.
//═══════════════════════════════════════════════════════════════
class TextCleaner {
private:
    static constexpr std::string_view START_MARKER = "*** START OF";
    static constexpr std::string_view END_MARKER = "*** END OF";

public:
    // Removes Gutenberg header/footer markers ("*** START OF" / "*** END OF").
    // Uses memmem() for speed, modifies string in-place with one memmove+resize.
    static inline void clean_text_inplace(std::string& text) {
        size_t start_pos = 0;
        size_t end_pos = text.size();

        // Search for START marker with memmem (faster than find)
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

        // Search for END marker
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

        // Erase in-place (single operation)
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
// Fast text normalization using a lookup table (no repeated tolower/isalpha calls),
// in-place modification with read/write pointers, and UTF-8 accent handling.
//═══════════════════════════════════════════════════════════════
class Tokenizer {
private:
    // Precomputed character info to avoid calling tolower/isalpha repeatedly
    struct CharInfo {
        unsigned char lower;  // Pre-lowercased version
        uint8_t flags;        // Bit flags: 0=alpha, 1=space, 2=punct, 3=digit
    };

    // Returns a static 256-entry lookup table for ASCII chars.
    // Built once on first call, then reused. No runtime classification overhead.
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

    // Converts UTF-8 accented chars to ASCII (é→e, ñ→n, etc.) for normalization
    static inline std::string_view process_utf8_char(const unsigned char* bytes, size_t& skip) {
        skip = 0;
        if ((bytes[0] & 0xE0) == 0xC0 && bytes[1]) {  // 2-byte UTF-8 sequence
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
    // Normalizes text in-place: lowercase, remove digits, handle UTF-8 accents.
    // Two-pointer approach (read/write) avoids allocations, resize only at the end.
    static void normalize_inplace(std::string& text, bool remove_punct = false) {
        const CharInfo* table = get_char_table();
        char* write = &text[0];  // Write pointer
        const char* read = text.data();  // Read pointer
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

            if (info.flags & 8) continue; // is_digit

            if (remove_punct && (info.flags & 4)) {
                *write++ = ' ';
            } else if (info.flags & 1) { // is_alpha
                *write++ = info.lower;
            } else if (info.flags & 2) { // is_space
                *write++ = ' ';
            }
        }

        text.resize(write - &text[0]);
    }

    // Splits text into words using string_view (zero-copy), space/tab/newline as delimiters
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
// STRING POOL (String Interning + Arena Allocation)
// Each unique word stored once in a contiguous arena.
// Benefits: no duplicate strings, better cache locality, fast ID-based comparison.
//═══════════════════════════════════════════════════════════════
class OptimizedStringPool {
private:
    std::vector<char> arena;  // Single buffer for all strings
    std::vector<std::string_view> id_to_word;  // ID → word (fast lookup)
    std::unordered_map<std::string_view, size_t> word_to_id;  // word → ID (deduplication)

public:
    OptimizedStringPool() {
        arena.reserve(ARENA_INITIAL_SIZE);
        id_to_word.reserve(100'000);
        word_to_id.reserve(100'000);
    }

    // Returns existing ID or creates new entry. Uses string_view to avoid copying.
    inline size_t intern(std::string_view s) {
        auto it = word_to_id.find(s);
        if (it != word_to_id.end()) {  // Already interned
            return it->second;
        }

        size_t pos = arena.size();
        arena.insert(arena.end(), s.begin(), s.end());

        std::string_view view(arena.data() + pos, s.size());
        size_t new_id = id_to_word.size();
        id_to_word.push_back(view);
        word_to_id.emplace(view, new_id);
        return new_id;
    }

    inline std::string_view get(size_t id) const {
        return id_to_word[id];
    }

    size_t size() const { return id_to_word.size(); }
};

//═══════════════════════════════════════════════════════════════
// NGRAM ID
// Stores n-grams as numeric IDs instead of strings for fast comparison.
// Cache-aligned (32B) with explicit padding to prevent false sharing.
//═══════════════════════════════════════════════════════════════
struct alignas(32) NgramID {
    size_t word_ids[3];  // Up to 3 words (trigram)
    uint8_t length;      // Actual number of words (2 or 3)
    uint8_t padding[7];  // Padding for 32-byte alignment

    inline bool operator==(const NgramID& other) const noexcept {
        return length == other.length &&
               word_ids[0] == other.word_ids[0] &&
               (length < 2 || word_ids[1] == other.word_ids[1]) &&
               (length < 3 || word_ids[2] == other.word_ids[2]);
    }
};

// FNV-1a hash for NgramID - fast with good distribution
struct NgramIDHash {
    inline size_t operator()(const NgramID& n) const noexcept {
        size_t hash = 14695981039346656037ULL;  // FNV offset basis
        hash ^= n.length;
        hash *= 1099511628211ULL;

        for (size_t i = 0; i < n.length; ++i) {
            hash ^= n.word_ids[i];
            hash *= 1099511628211ULL;
        }
        return hash;
    }
};

//═══════════════════════════════════════════════════════════════
// HYBRID FREQUENCY COUNTER
// Two-phase design: hash map for accumulation (fast inserts),
// then convert to parallel arrays for sorting/queries (cache-friendly).
// Uses partial_sort for top-N: O(n log k) instead of O(n log n).
//═══════════════════════════════════════════════════════════════
class HybridFrequencyCounter {
private:
    OptimizedStringPool pool;
    std::vector<NgramID> ngram_ids;
    std::vector<size_t> frequencies;
    bool finalized = false;

public:
    // Converts from hash map (good for inserts) to parallel arrays (good for queries).
    // Parses each n-gram string ("word1 word2"), interns words to IDs.
    void build_from_aos(const std::unordered_map<std::string, size_t>& aos_map) {
        size_t total_size = aos_map.size();

        ngram_ids.clear();
        frequencies.clear();
        ngram_ids.reserve(total_size);  // Pre-allocation to avoid resize
        frequencies.reserve(total_size);

        for (const auto& [ngram_str, freq] : aos_map) {
            NgramID id;
            id.length = 0;

            const char* str = ngram_str.data();
            const size_t len = ngram_str.size();
            size_t start = 0;

            for (size_t pos = 0; pos <= len && id.length < 3; ++pos) {
                if (pos == len || str[pos] == ' ') {
                    if (pos > start) {
                        std::string_view word(str + start, pos - start);
                        id.word_ids[id.length++] = pool.intern(word);
                    }
                    start = pos + 1;
                }
            }

            ngram_ids.push_back(id);
            frequencies.push_back(freq);
        }

        finalized = true;
    }

    // Returns top N n-grams using partial_sort (O(n log k) vs O(n log n)).
    // Important when k << n (e.g., top 20 from 100k elements).
    std::vector<std::pair<std::string, size_t>> get_top_n(size_t n) const {
        if (!finalized) {
            throw std::runtime_error("Call build_from_aos() first!");
        }

        std::vector<size_t> indices(frequencies.size());
        std::iota(indices.begin(), indices.end(), 0);  // [0, 1, 2, ...]

        size_t k = std::min(n, indices.size());

        std::partial_sort(  // Sort only the first k elements
            indices.begin(),
            indices.begin() + k,
            indices.end(),
            [this](size_t i, size_t j) {
                return frequencies[i] > frequencies[j];
            }
        );

        std::vector<std::pair<std::string, size_t>> result;
        result.reserve(k);

        for (size_t i = 0; i < k; ++i) {
            size_t idx = indices[i];
            const auto& id = ngram_ids[idx];

            std::string ngram;
            ngram.reserve(64);

            for (size_t j = 0; j < id.length; ++j) {
                if (j > 0) ngram += ' ';
                auto word = pool.get(id.word_ids[j]);
                ngram.append(word.data(), word.size());
            }

            result.emplace_back(std::move(ngram), frequencies[idx]);
        }

        return result;
    }

    std::vector<std::pair<std::string, size_t>> get_all_sorted() const {
        if (!finalized) {
            throw std::runtime_error("Call build_from_aos() first!");
        }

        std::vector<size_t> indices(frequencies.size());
        std::iota(indices.begin(), indices.end(), 0);

        std::sort(
            indices.begin(),
            indices.end(),
            [this](size_t i, size_t j) {
                return frequencies[i] > frequencies[j];
            }
        );

        std::vector<std::pair<std::string, size_t>> result;
        result.reserve(indices.size());

        for (size_t idx : indices) {
            const auto& id = ngram_ids[idx];

            std::string ngram;
            ngram.reserve(64);

            for (size_t j = 0; j < id.length; ++j) {
                if (j > 0) ngram += ' ';
                auto word = pool.get(id.word_ids[j]);
                ngram.append(word.data(), word.size());
            }

            result.emplace_back(std::move(ngram), frequencies[idx]);
        }

        return result;
    }

    size_t total_unique() const { return ngram_ids.size(); }

    size_t total_count() const {
        return std::accumulate(frequencies.begin(), frequencies.end(), 0ULL);
    }
};

//═══════════════════════════════════════════════════════════════
// CSV SAVER (with buffered I/O)
//═══════════════════════════════════════════════════════════════
class CSVSaver {
public:
    static void save_ngrams(
        const HybridFrequencyCounter& counter,
        const std::string& filename,
        const std::string& label
    ) {
        auto all_ngrams = counter.get_all_sorted();

        std::ofstream out(filename);
        if (!out) {
            std::cerr << "Error opening file: " << filename << "\n";
            return;
        }

        // 64KB buffer to reduce system calls
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
// SEQUENTIAL PROCESSOR
// Single-threaded baseline. Pipeline: read → clean → normalize → tokenize → count n-grams.
//═══════════════════════════════════════════════════════════════
class SequentialProcessor {
public:
    // Processes one text: normalize, tokenize, generate n-grams.
    // Uses pre-allocated buffers to avoid reallocations.
    static void process_text_aos_optimized(
        std::string& text,
        std::unordered_map<std::string, size_t>& word_bigrams,
        std::unordered_map<std::string, size_t>& word_trigrams,
        std::unordered_map<std::string, size_t>& char_bigrams,
        std::unordered_map<std::string, size_t>& char_trigrams,
        std::vector<std::string_view>& words_buffer,
        std::vector<char>& chars_buffer
    ) {
        Tokenizer::normalize_inplace(text, true);

        words_buffer.clear();
        Tokenizer::tokenize_words(text, words_buffer);

        std::string key;
        key.reserve(128);

        const size_t word_count = words_buffer.size();

        // WORD BIGRAMS - sliding window, reusing string buffer
        for (size_t i = 0; i + 1 < word_count; ++i) {
            const auto& w1 = words_buffer[i];
            const auto& w2 = words_buffer[i + 1];

            key.clear();
            key.append(w1);
            key.push_back(' ');
            key.append(w2);
            ++word_bigrams[key];
        }

        // WORD TRIGRAMS
        for (size_t i = 0; i + 2 < word_count; ++i) {
            const auto& w1 = words_buffer[i];
            const auto& w2 = words_buffer[i + 1];
            const auto& w3 = words_buffer[i + 2];

            key.clear();
            key.append(w1);
            key.push_back(' ');
            key.append(w2);
            key.push_back(' ');
            key.append(w3);
            ++word_trigrams[key];
        }

        chars_buffer.clear();
        Tokenizer::tokenize_chars(text, chars_buffer);

        const size_t char_count = chars_buffer.size();

        // CHAR BIGRAMS - fixed "c1 c2" format (3 chars)
        key.resize(3);
        key[1] = ' ';

        for (size_t i = 0; i + 1 < char_count; ++i) {
            key[0] = chars_buffer[i];
            key[2] = chars_buffer[i + 1];
            ++char_bigrams[key];
        }

        // CHAR TRIGRAMS
        key.resize(5);
        key[1] = ' ';
        key[3] = ' ';

        for (size_t i = 0; i + 2 < char_count; ++i) {
            key[0] = chars_buffer[i];
            key[2] = chars_buffer[i + 1];
            key[4] = chars_buffer[i + 2];
            ++char_trigrams[key];
        }
    }

    // Main pipeline: accumulate in hash maps, convert to arrays at the end.
    // Pre-allocates containers based on typical workload.
    static void process_sequential(
        const std::vector<std::string>& book_files,
        HybridFrequencyCounter& word_bigrams,
        HybridFrequencyCounter& word_trigrams,
        HybridFrequencyCounter& char_bigrams,
        HybridFrequencyCounter& char_trigrams
    ) {
        int total_books = book_files.size();

        // Hash map for accumulation phase (AoS)
        std::unordered_map<std::string, size_t> wb, wt, cb, ct;
        wb.reserve(WB_RESERVE);  // Pre-allocation to avoid rehash
        wt.reserve(WT_RESERVE);
        cb.reserve(CB_RESERVE);
        ct.reserve(CT_RESERVE);

        std::vector<std::string_view> words_buf;
        std::vector<char> chars_buf;
        words_buf.reserve(WORD_BUFFER_SIZE);
        chars_buf.reserve(CHAR_BUFFER_SIZE);

        for (int i = 0; i < total_books; ++i) {
            const auto& filepath = book_files[i];

            std::ifstream file(filepath, std::ios::binary | std::ios::ate);
            if (!file) continue;

            std::streamsize size = file.tellg();
            file.seekg(0, std::ios::beg);
            std::string text(size, '\0');
            if (!file.read(&text[0], size)) continue;

            TextCleaner::clean_text_inplace(text);

            process_text_aos_optimized(text, wb, wt, cb, ct, words_buf, chars_buf);
        }

        word_bigrams.build_from_aos(wb);
        word_trigrams.build_from_aos(wt);
        char_bigrams.build_from_aos(cb);
        char_trigrams.build_from_aos(ct);
    }
};

//═══════════════════════════════════════════════════════════════
// MAIN
//═══════════════════════════════════════════════════════════════
int main() {


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

    std::cout << "Found " << book_files.size() << " books\n";

    // Benchmark: warm-up runs for cache, then measured runs for stats
    const int WARMUP_RUNS = 2;
    const int MEASURED_RUNS = 10;
    const int NUM_RUNS = WARMUP_RUNS + MEASURED_RUNS;

    struct RunResult { double wall; double cpu; bool warmup; };
    std::vector<RunResult> run_times;
    run_times.reserve(NUM_RUNS);



    HybridFrequencyCounter word_bigrams, word_trigrams, char_bigrams, char_trigrams;

    // Run multiple times: warm-up (discarded) + measured runs (for stats)
    for (int run = 0; run < NUM_RUNS; ++run) {
        // Reset for clean measurement
        word_bigrams = HybridFrequencyCounter();
        word_trigrams = HybridFrequencyCounter();
        char_bigrams = HybridFrequencyCounter();
        char_trigrams = HybridFrequencyCounter();

        // Measure both wall-clock (includes I/O) and CPU time
        auto start_time = std::chrono::high_resolution_clock::now();
        std::clock_t start_cpu = std::clock();

        SequentialProcessor::process_sequential(
            book_files,
            word_bigrams,
            word_trigrams,
            char_bigrams,
            char_trigrams
        );

        auto end_time = std::chrono::high_resolution_clock::now();
        std::clock_t end_cpu = std::clock();
        std::chrono::duration<double> elapsed = end_time - start_time;
        double cpu_seconds = double(end_cpu - start_cpu) / double(CLOCKS_PER_SEC);

        run_times.push_back(RunResult{elapsed.count(), cpu_seconds, run < WARMUP_RUNS});

        std::cout << "  [Run " << (run + 1) << "/" << NUM_RUNS;
        if (run < WARMUP_RUNS) {
            std::cout << " WARM-UP] ";
        } else {
            std::cout << "] ";
        }
        std::cout << std::fixed << std::setprecision(2) << elapsed.count()
                  << "s (cpu: " << cpu_seconds << "s)\n";
    }

    // Extract measured runs (skip warm-up)
    std::vector<double> measured_times;
    std::vector<double> measured_cpu;
    for (const auto& r : run_times) {
        if (!r.warmup) {
            measured_times.push_back(r.wall);
            measured_cpu.push_back(r.cpu);
        }
    }

    // Compute stats: mean, min, max, stddev, CV (coefficient of variation)
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
    for (double t : measured_cpu) stddev_cpu += (t - mean_cpu) * (t - mean_cpu);
    stddev_cpu = std::sqrt(stddev_cpu / measured_cpu.size());
    double cv_cpu = (stddev_cpu / mean_cpu) * 100.0;


    std::string output_dir = "results/sequential";
    ensure_directory_exists(output_dir);




    CSVSaver::save_ngrams(word_bigrams, output_dir + "/word_bigrams.csv", "Word Bigrams");
    CSVSaver::save_ngrams(word_trigrams, output_dir + "/word_trigrams.csv", "Word Trigrams");
    CSVSaver::save_ngrams(char_bigrams, output_dir + "/char_bigrams.csv", "Char Bigrams");
    CSVSaver::save_ngrams(char_trigrams, output_dir + "/char_trigrams.csv", "Char Trigrams");

    // Save performance statistics
    std::ofstream stats_file(output_dir + "/performance_stats.txt");
    if (stats_file) {
        stats_file << "PERFORMANCE SUMMARY (" << MEASURED_RUNS << " runs)\n";
        stats_file << "Threads: 1 (Sequential)\n\n";
        
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
        stats_file << "  Word Bigrams:  " << word_bigrams.total_unique() << "\n";
        stats_file << "  Word Trigrams: " << word_trigrams.total_unique() << "\n";
        stats_file << "  Char Bigrams:  " << char_bigrams.total_unique() << "\n";
        stats_file << "  Char Trigrams: " << char_trigrams.total_unique() << "\n";
        
        stats_file.close();
        std::cout << "\nPerformance statistics saved to " << output_dir << "/performance_stats.txt\n";
    }

    return 0;
}