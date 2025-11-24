//
// Created by Lorenzo Cappetti on 21/11/25.
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

namespace fs = std::filesystem;

//═══════════════════════════════════════════════════════════════
// COMPILE-TIME CONSTANTS
//═══════════════════════════════════════════════════════════════
constexpr size_t ARENA_INITIAL_SIZE = 20'000'000;
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
// OPTIMIZED TEXT CLEANER - Zero-copy with string_view
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
// ULTRA-OPTIMIZED TOKENIZER with SIMD-friendly operations
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
// LOCK-FREE STRING POOL with better memory layout
//═══════════════════════════════════════════════════════════════
class OptimizedStringPool {
private:
    std::vector<char> arena;
    std::vector<std::string_view> id_to_word;
    std::unordered_map<std::string_view, size_t> word_to_id;

public:
    OptimizedStringPool() {
        arena.reserve(ARENA_INITIAL_SIZE);
        id_to_word.reserve(100'000);
        word_to_id.reserve(100'000);
    }

    inline size_t intern(std::string_view s) {
        auto it = word_to_id.find(s);
        if (it != word_to_id.end()) {
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
// NGRAM ID - Cache-aligned
//═══════════════════════════════════════════════════════════════
struct alignas(32) NgramID {
    size_t word_ids[3];
    uint8_t length;
    uint8_t padding[7];

    inline bool operator==(const NgramID& other) const noexcept {
        return length == other.length &&
               word_ids[0] == other.word_ids[0] &&
               (length < 2 || word_ids[1] == other.word_ids[1]) &&
               (length < 3 || word_ids[2] == other.word_ids[2]);
    }
};

struct NgramIDHash {
    inline size_t operator()(const NgramID& n) const noexcept {
        size_t hash = 14695981039346656037ULL;
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
// HYBRID FREQUENCY COUNTER - Optimized sorting
//═══════════════════════════════════════════════════════════════
class HybridFrequencyCounter {
private:
    OptimizedStringPool pool;
    std::vector<NgramID> ngram_ids;
    std::vector<size_t> frequencies;
    bool finalized = false;

public:
    void build_from_aos(const std::unordered_map<std::string, size_t>& aos_map) {
        size_t total_size = aos_map.size();

        ngram_ids.clear();
        frequencies.clear();
        ngram_ids.reserve(total_size);
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

    std::vector<std::pair<std::string, size_t>> get_top_n(size_t n) const {
        if (!finalized) {
            throw std::runtime_error("Call build_from_aos() first!");
        }

        std::vector<size_t> indices(frequencies.size());
        std::iota(indices.begin(), indices.end(), 0);

        size_t k = std::min(n, indices.size());

        std::partial_sort(
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
// CSV SAVER - Buffered writes
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
// ULTRA-OPTIMIZED OPENMP PROCESSOR
//═══════════════════════════════════════════════════════════════
class OptimizedOpenMPProcessor {
public:
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

        for (size_t i = 0; i + 1 < word_count; ++i) {
            const auto& w1 = words_buffer[i];
            const auto& w2 = words_buffer[i + 1];

            key.clear();
            key.append(w1);
            key.push_back(' ');
            key.append(w2);
            ++word_bigrams[key];
        }

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

        key.resize(3);
        key[1] = ' ';

        for (size_t i = 0; i + 1 < char_count; ++i) {
            key[0] = chars_buffer[i];
            key[2] = chars_buffer[i + 1];
            ++char_bigrams[key];
        }

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

    static void parallel_merge_aos(
        std::vector<std::unordered_map<std::string, size_t>>& thread_maps,
        std::unordered_map<std::string, size_t>& result
    ) {
        if (thread_maps.empty()) return;
        if (thread_maps.size() == 1) {
            result = std::move(thread_maps[0]);
            return;
        }

        size_t active_maps = thread_maps.size();

        while (active_maps > 1) {
            size_t pairs = active_maps / 2;

            #pragma omp parallel for schedule(dynamic)
            for (size_t i = 0; i < pairs; ++i) {
                auto& target = thread_maps[i];
                auto& source = thread_maps[i + pairs];

                if (source.size() > target.size()) {
                    std::swap(target, source);
                }

                target.reserve(target.size() + source.size());
                for (auto& [key, val] : source) {
                    target[key] += val;
                }
                source.clear();
            }

            if (active_maps % 2 == 1) {
                if (pairs > 0) {
                    auto& odd_map = thread_maps[active_maps - 1];
                    auto& target_map = thread_maps[pairs];

                    if (odd_map.size() > target_map.size()) {
                        std::swap(target_map, odd_map);
                    }

                    target_map.reserve(target_map.size() + odd_map.size());
                    for (auto& [key, val] : odd_map) {
                        target_map[key] += val;
                    }
                    odd_map.clear();
                }
                active_maps = pairs + 1;
            } else {
                active_maps = pairs;
            }
        }

        result = std::move(thread_maps[0]);
    }

    static void process_parallel_hybrid(
        const std::vector<std::string>& book_files,
        HybridFrequencyCounter& word_bigrams,
        HybridFrequencyCounter& word_trigrams,
        HybridFrequencyCounter& char_bigrams,
        HybridFrequencyCounter& char_trigrams,
        int num_threads = 0
    ) {
        if (num_threads == 0) num_threads = omp_get_max_threads();
        omp_set_num_threads(num_threads);

        int total_books = book_files.size();

        std::vector<std::unordered_map<std::string, size_t>> thread_wb(num_threads);
        std::vector<std::unordered_map<std::string, size_t>> thread_wt(num_threads);
        std::vector<std::unordered_map<std::string, size_t>> thread_cb(num_threads);
        std::vector<std::unordered_map<std::string, size_t>> thread_ct(num_threads);

        #pragma omp parallel default(none) shared(book_files, total_books, thread_wb, thread_wt, thread_cb, thread_ct)
        {
            int tid = omp_get_thread_num();
            auto& my_wb = thread_wb[tid];
            auto& my_wt = thread_wt[tid];
            auto& my_cb = thread_cb[tid];
            auto& my_ct = thread_ct[tid];

            my_wb.reserve(WB_RESERVE);
            my_wt.reserve(WT_RESERVE);
            my_cb.reserve(CB_RESERVE);
            my_ct.reserve(CT_RESERVE);

            std::vector<std::string_view> words_buf;
            std::vector<char> chars_buf;
            words_buf.reserve(WORD_BUFFER_SIZE);
            chars_buf.reserve(CHAR_BUFFER_SIZE);

            #pragma omp for schedule(dynamic, 1) nowait
            for (int i = 0; i < total_books; ++i) {
                const auto& filepath = book_files[i];

                std::ifstream file(filepath, std::ios::binary | std::ios::ate);
                if (!file) continue;

                std::streamsize size = file.tellg();
                file.seekg(0, std::ios::beg);
                std::string text(size, '\0');
                if (!file.read(&text[0], size)) continue;

                TextCleaner::clean_text_inplace(text);

                process_text_aos_optimized(text, my_wb, my_wt, my_cb, my_ct,
                                          words_buf, chars_buf);
            }
        }

        std::unordered_map<std::string, size_t> merged_wb, merged_wt, merged_cb, merged_ct;

        parallel_merge_aos(thread_wb, merged_wb);
        parallel_merge_aos(thread_wt, merged_wt);
        parallel_merge_aos(thread_cb, merged_cb);
        parallel_merge_aos(thread_ct, merged_ct);

        #pragma omp parallel sections default(none) shared(word_bigrams, word_trigrams, char_bigrams, char_trigrams, merged_wb, merged_wt, merged_cb, merged_ct)
        {
            #pragma omp section
            word_bigrams.build_from_aos(merged_wb);

            #pragma omp section
            word_trigrams.build_from_aos(merged_wt);

            #pragma omp section
            char_bigrams.build_from_aos(merged_cb);

            #pragma omp section
            char_trigrams.build_from_aos(merged_ct);
        }
    }
};

//═══════════════════════════════════════════════════════════════
// MAIN
//═══════════════════════════════════════════════════════════════
int main()
{
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
    const int MAX_VIRTUAL_THREADS = 32;
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

    const int WARMUP_RUNS = 0;
    const int MEASURED_RUNS = 1;
    const int NUM_RUNS = WARMUP_RUNS + MEASURED_RUNS;

    struct RunResult { double wall; double cpu; bool warmup; };
    std::vector<RunResult> run_times;
    run_times.reserve(NUM_RUNS);

    HybridFrequencyCounter word_bigrams, word_trigrams, char_bigrams, char_trigrams;

    for (int run = 0; run < NUM_RUNS; ++run) {
        word_bigrams = HybridFrequencyCounter();
        word_trigrams = HybridFrequencyCounter();
        char_bigrams = HybridFrequencyCounter();
        char_trigrams = HybridFrequencyCounter();

        auto start_time = std::chrono::high_resolution_clock::now();
        std::clock_t start_cpu = std::clock();

        OptimizedOpenMPProcessor::process_parallel_hybrid(
            book_files,
            word_bigrams,
            word_trigrams,
            char_bigrams,
            char_trigrams,
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

    double mean_cpu = 0.0;
    for (double t : measured_cpu) {
        mean_cpu += t;
    }
    mean_cpu /= measured_cpu.size();

    std::string output_dir = "test/output_hybrid";
    ensure_directory_exists(output_dir);



    CSVSaver::save_ngrams(word_bigrams, output_dir + "/word_bigrams.csv", "Word Bigrams");
    CSVSaver::save_ngrams(word_trigrams, output_dir + "/word_trigrams.csv", "Word Trigrams");
    CSVSaver::save_ngrams(char_bigrams, output_dir + "/char_bigrams.csv", "Char Bigrams");
    CSVSaver::save_ngrams(char_trigrams, output_dir + "/char_trigrams.csv", "Char Trigrams");



    return 0;
}