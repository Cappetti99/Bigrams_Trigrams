//
// Lorenzo Cappetti, 2025 - Parallel Version with Thread-Safe Hash Table
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
#include <regex>
#include <chrono>
#include <thread>
#include <mutex>
#include <atomic>

namespace fs = std::filesystem;

// ==================== TEXT CLEANER ====================
class TextCleaner {
public:
    static std::string remove_gutenberg_header(const std::string& text) {
        std::string result = text;
        size_t start_pos = result.find("*** START OF");
        if (start_pos != std::string::npos) {
            size_t end_of_line = result.find("***", start_pos + 12);
            if (end_of_line != std::string::npos) {
                result = result.substr(end_of_line + 3);
            }
        }
        size_t contents_pos = result.find("Contents");
        if (contents_pos != std::string::npos) {
            std::regex chapter_pattern(R"(CHAPTER\s+[IVX]+\.)", std::regex::icase);
            std::smatch match;
            std::string temp = result.substr(contents_pos);
            if (std::regex_search(temp, match, chapter_pattern)) {
                result = result.substr(0, contents_pos) + temp.substr(match.position());
            }
        }
        return result;
    }

    static std::string remove_gutenberg_footer(const std::string& text) {
        std::regex end_pattern(R"(\*\*\* END OF.*)", std::regex::icase);
        return std::regex_replace(text, end_pattern, "");
    }

    static std::string clean_text(const std::string& text) {
        std::string cleaned = remove_gutenberg_header(text);
        cleaned = remove_gutenberg_footer(cleaned);
        return cleaned;
    }
};

// ==================== TOKENIZER ====================
class Tokenizer {
private:
    static std::string process_utf8_char(const unsigned char* bytes, size_t& skip) {
        skip = 0;
        if ((bytes[0] & 0xE0) == 0xC0 && bytes[1]) {
            skip = 2;
            unsigned char first = bytes[0];
            unsigned char second = bytes[1];
            if ((first == 0xC3 && second >= 0x80 && second <= 0x85) ||
                (first == 0xC3 && second >= 0xA0 && second <= 0xA5)) return "a";
            if ((first == 0xC3 && second >= 0x88 && second <= 0x8B) ||
                (first == 0xC3 && second >= 0xA8 && second <= 0xAB)) return "e";
            if ((first == 0xC3 && second >= 0x8C && second <= 0x8F) ||
                (first == 0xC3 && second >= 0xAC && second <= 0xAF)) return "i";
            if ((first == 0xC3 && second >= 0x92 && second <= 0x96) ||
                (first == 0xC3 && second >= 0xB2 && second <= 0xB6)) return "o";
            if ((first == 0xC3 && second >= 0x99 && second <= 0x9C) ||
                (first == 0xC3 && second >= 0xB9 && second <= 0xBC)) return "u";
            if ((first == 0xC3 && second == 0x91) || (first == 0xC3 && second == 0xB1)) return "n";
            if ((first == 0xC3 && second == 0x87) || (first == 0xC3 && second == 0xA7)) return "c";
            if ((first == 0xC3 && (second == 0x9D || second == 0xBD || second == 0x9F || second == 0xBF))) return "y";
            return "";
        }
        if ((bytes[0] & 0xF0) == 0xE0 && bytes[1] && bytes[2]) {
            skip = 3;
            if (bytes[0] == 0xE2 && bytes[1] == 0x80 &&
                (bytes[2] >= 0x98 && bytes[2] <= 0x9F)) return " ";
            return "";
        }
        if ((bytes[0] & 0xF8) == 0xF0 && bytes[1] && bytes[2] && bytes[3]) {
            skip = 4;
            return "";
        }
        return "";
    }

public:
    static std::string normalize(const std::string& text, bool remove_punct = false) {
        std::string result;
        result.reserve(text.size());
        for (size_t i = 0; i < text.size(); ++i) {
            unsigned char c = static_cast<unsigned char>(text[i]);
            if (c >= 0x80) {
                size_t skip;
                std::string replacement = process_utf8_char(
                    reinterpret_cast<const unsigned char*>(&text[i]), skip);
                if (skip > 0) {
                    result += replacement;
                    i += skip - 1;
                    continue;
                }
            }
            if (std::isdigit(c)) continue;
            if (remove_punct && std::ispunct(c)) {
                result += ' ';
                continue;
            }
            if (std::isalpha(c) || std::isspace(c))
                result += std::tolower(c);
        }
        return result;
    }

    static std::vector<std::string> tokenize_words(const std::string& text) {
        std::vector<std::string> tokens;
        std::istringstream iss(text);
        std::string word;
        while (iss >> word) {
            if (!word.empty()) tokens.push_back(word);
        }
        return tokens;
    }

    static std::vector<char> tokenize_chars(const std::string& text) {
        std::vector<char> chars;
        for (char c : text) {
            if (!std::isspace(static_cast<unsigned char>(c))) {
                chars.push_back(c);
            }
        }
        return chars;
    }
};

// ==================== N-GRAM EXTRACTOR ====================
template<typename T>
class NgramExtractor {
public:
    static std::vector<std::vector<T>> extract(const std::vector<T>& tokens, size_t n) {
        std::vector<std::vector<T>> ngrams;
        if (tokens.size() < n) return ngrams;
        for (size_t i = 0; i <= tokens.size() - n; ++i) {
            ngrams.emplace_back(tokens.begin() + i, tokens.begin() + i + n);
        }
        return ngrams;
    }

    static std::string ngram_to_string(const std::vector<T>& ngram) {
        std::ostringstream oss;
        for (size_t i = 0; i < ngram.size(); ++i) {
            if (i > 0) oss << " ";
            oss << ngram[i];
        }
        return oss.str();
    }
};

// ==================== THREAD-SAFE FREQUENCY COUNTER ====================
class ThreadSafeFrequencyCounter {
private:
    std::unordered_map<std::string, size_t> frequencies;
    mutable std::mutex mtx;

public:
    // Aggiunge un n-gram in modo thread-safe
    void add_ngram(const std::string& ngram) {
        std::lock_guard<std::mutex> lock(mtx);
        frequencies[ngram]++;
    }

    // Unisce frequenze locali in batch (più efficiente)
    void merge_local(const std::unordered_map<std::string, size_t>& local_freqs) {
        std::lock_guard<std::mutex> lock(mtx);
        for (const auto& [ngram, count] : local_freqs) {
            frequencies[ngram] += count;
        }
    }

    std::vector<std::pair<std::string, size_t>> get_top_n(size_t n) const {
        std::lock_guard<std::mutex> lock(mtx);
        std::vector<std::pair<std::string, size_t>> sorted(frequencies.begin(), frequencies.end());
        std::partial_sort(
            sorted.begin(),
            sorted.begin() + std::min(n, sorted.size()),
            sorted.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; }
        );
        sorted.resize(std::min(n, sorted.size()));
        return sorted;
    }

    size_t total_unique() const {
        std::lock_guard<std::mutex> lock(mtx);
        return frequencies.size();
    }

    size_t total_count() const {
        std::lock_guard<std::mutex> lock(mtx);
        size_t total = 0;
        for (const auto& [_, count] : frequencies) total += count;
        return total;
    }

    const std::unordered_map<std::string, size_t> get_frequencies_copy() const {
        std::lock_guard<std::mutex> lock(mtx);
        return frequencies;
    }
};

// ==================== PARALLEL PROCESSOR ====================
class ParallelProcessor {
public:
    // Processa un singolo testo e accumula in una hash table locale
    static void process_text_local(
        const std::string& text,
        size_t n,
        bool use_words,
        std::unordered_map<std::string, size_t>& local_counter
    ) {
        std::string normalized = Tokenizer::normalize(text, true);

        if (use_words) {
            auto tokens = Tokenizer::tokenize_words(normalized);
            auto ngrams = NgramExtractor<std::string>::extract(tokens, n);
            for (const auto& ng : ngrams) {
                local_counter[NgramExtractor<std::string>::ngram_to_string(ng)]++;
            }
        } else {
            auto tokens = Tokenizer::tokenize_chars(normalized);
            auto ngrams = NgramExtractor<char>::extract(tokens, n);
            for (const auto& ng : ngrams) {
                local_counter[NgramExtractor<char>::ngram_to_string(ng)]++;
            }
        }
    }

    // Thread worker: processa un batch di libri
    static void worker_thread(
        const std::vector<std::string>& book_files,
        size_t start_idx,
        size_t end_idx,
        ThreadSafeFrequencyCounter& word_bigrams,
        ThreadSafeFrequencyCounter& word_trigrams,
        ThreadSafeFrequencyCounter& char_bigrams,
        ThreadSafeFrequencyCounter& char_trigrams,
        std::atomic<size_t>& processed_count
    ) {
        for (size_t i = start_idx; i < end_idx; ++i) {
            const auto& filepath = book_files[i];

            std::ifstream file(filepath);
            if (!file) {
                std::cerr << "Impossibile aprire: " << filepath << "\n";
                continue;
            }

            std::stringstream buffer;
            buffer << file.rdbuf();
            std::string text = buffer.str();
            text = TextCleaner::clean_text(text);

            // Hash table locali per ridurre contention sui lock
            std::unordered_map<std::string, size_t> local_wb, local_wt, local_cb, local_ct;

            process_text_local(text, 2, true, local_wb);
            process_text_local(text, 3, true, local_wt);
            process_text_local(text, 2, false, local_cb);
            process_text_local(text, 3, false, local_ct);

            // Merge in batch alle strutture globali
            word_bigrams.merge_local(local_wb);
            word_trigrams.merge_local(local_wt);
            char_bigrams.merge_local(local_cb);
            char_trigrams.merge_local(local_ct);

            processed_count++;
        }
    }

    // Avvia l'elaborazione parallela
    static void process_parallel(
        const std::vector<std::string>& book_files,
        ThreadSafeFrequencyCounter& word_bigrams,
        ThreadSafeFrequencyCounter& word_trigrams,
        ThreadSafeFrequencyCounter& char_bigrams,
        ThreadSafeFrequencyCounter& char_trigrams,
        size_t num_threads = 0
    ) {
        if (num_threads == 0)
            num_threads = std::thread::hardware_concurrency();
        if (num_threads == 0) num_threads = 4;

        std::cout << "Usando " << num_threads << " threads\n\n";

        std::atomic<size_t> processed_count{0};
        std::vector<std::thread> threads;

        size_t books_per_thread = book_files.size() / num_threads;
        size_t remainder = book_files.size() % num_threads;

        size_t start = 0;
        for (size_t t = 0; t < num_threads; ++t) {
            size_t end = start + books_per_thread + (t < remainder ? 1 : 0);
            if (start >= book_files.size()) break;

            threads.emplace_back(
                worker_thread,
                std::cref(book_files),
                start, end,
                std::ref(word_bigrams),
                std::ref(word_trigrams),
                std::ref(char_bigrams),
                std::ref(char_trigrams),
                std::ref(processed_count)
            );
            start = end;
        }

        // Progress monitor
        std::thread monitor([&]() {
            while (processed_count < book_files.size()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                std::cout << "\rProcessati: " << processed_count << "/"
                          << book_files.size() << " libri" << std::flush;
            }
            std::cout << "\rProcessati: " << book_files.size() << "/"
                      << book_files.size() << " libri\n";
        });

        for (auto& t : threads) t.join();
        monitor.join();
    }
};

// ==================== STATISTICS GENERATOR ====================
class StatisticsGenerator {
public:
    static void print_statistics(const ThreadSafeFrequencyCounter& counter,
                                const std::string& type,
                                size_t n,
                                size_t top_n = 20) {
        std::cout << "\n========== " << type << " (n=" << n << ") ==========\n";
        std::cout << "Total unique " << type << ": " << counter.total_unique() << "\n";
        std::cout << "Total count: " << counter.total_count() << "\n\n";
        std::cout << "Top " << top_n << " most frequent:\n";
        auto top = counter.get_top_n(top_n);
        for (size_t i = 0; i < top.size(); ++i) {
            auto [ngram, freq] = top[i];
            std::cout << (i + 1) << ". \"" << ngram << "\" - " << freq << " occurrences\n";
        }
    }

    static void save_to_file(const ThreadSafeFrequencyCounter& counter, const std::string& filename) {
        std::ofstream out(filename);
        if (!out) {
            std::cerr << "Errore nell'aprire il file: " << filename << "\n";
            return;
        }
        auto top = counter.get_top_n(counter.total_unique());
        out << "ngram,frequency\n";
        for (const auto& [ngram, freq] : top) {
            out << "\"" << ngram << "\"," << freq << "\n";
        }
        std::cout << "Statistiche salvate in: " << filename << "\n";
    }
};

// ==================== MAIN ====================
int main() {
    std::cout << "Parallel N-gram Analyzer with Thread-Safe Hash Tables\n";
    std::cout << "======================================================\n\n";

    std::string folder_path = "/Users/lorenzocappetti/CLionProjects/Bigrams_Trigrams/book_gutenberg";

    if (!fs::exists(folder_path) || !fs::is_directory(folder_path)) {
        std::cerr << "Errore: cartella '" << folder_path << "' non trovata!\n";
        return 1;
    }

    std::vector<std::string> book_files;
    for (const auto& entry : fs::directory_iterator(folder_path)) {
        if (entry.path().extension() == ".txt")
            book_files.push_back(entry.path().string());
    }

    if (book_files.empty()) {
        std::cerr << "Nessun file .txt trovato nella cartella!\n";
        return 1;
    }

    std::cout << "Trovati " << book_files.size() << " libri da processare\n";

    ThreadSafeFrequencyCounter word_bigrams, word_trigrams, char_bigrams, char_trigrams;

    auto start_time = std::chrono::high_resolution_clock::now();

    ParallelProcessor::process_parallel(
        book_files,
        word_bigrams, word_trigrams,
        char_bigrams, char_trigrams
    );

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end_time - start_time;

    StatisticsGenerator::print_statistics(word_bigrams, "Word Bigrams", 2);
    StatisticsGenerator::print_statistics(word_trigrams, "Word Trigrams", 3);
    StatisticsGenerator::print_statistics(char_bigrams, "Char Bigrams", 2);
    StatisticsGenerator::print_statistics(char_trigrams, "Char Trigrams", 3);

    std::cout << "\nTempo totale di esecuzione: " << elapsed.count() << " secondi\n";

    StatisticsGenerator::save_to_file(word_bigrams, "word_bigrams.csv");
    StatisticsGenerator::save_to_file(word_trigrams, "word_trigrams.csv");
    StatisticsGenerator::save_to_file(char_bigrams, "char_bigrams.csv");
    StatisticsGenerator::save_to_file(char_trigrams, "char_trigrams.csv");

    return 0;
}