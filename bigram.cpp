//
// Lorenzo Cappetti, 2025
//
#include <iostream>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <omp.h>

// ==================== TOKENIZER ====================
class Tokenizer {
public:
    static std::string normalize(const std::string& text, bool remove_punct = false) {
        std::string result;
        result.reserve(text.size());
        for (char c : text) {
            if (remove_punct && std::ispunct(static_cast<unsigned char>(c))) {
                result += ' ';
            } else {
                result += std::tolower(static_cast<unsigned char>(c));
            }
        }
        return result;
    } // #todo aggiungere roba

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
        bool last_was_space = false;
        for (char c : text) {
            if (std::isspace(static_cast<unsigned char>(c))) {
                if (!last_was_space) {
                    chars.push_back(' ');
                    last_was_space = true;
                }
            } else {
                chars.push_back(c);
                last_was_space = false;
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

// ==================== FREQUENCY COUNTER ====================
class FrequencyCounter {
private:
    std::unordered_map<std::string, size_t> frequencies;
    omp_lock_t lock;

public:
    FrequencyCounter() { omp_init_lock(&lock); }
    ~FrequencyCounter() { omp_destroy_lock(&lock); }

    FrequencyCounter(const FrequencyCounter&) = delete;
    FrequencyCounter& operator=(const FrequencyCounter&) = delete;

    void merge(const std::unordered_map<std::string, size_t>& other) {
        omp_set_lock(&lock);
        for (const auto& [ngram, count] : other) frequencies[ngram] += count;
        omp_unset_lock(&lock);
    }

    std::vector<std::pair<std::string, size_t>> get_top_n(size_t n) const {
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

    size_t total_unique() const { return frequencies.size(); }
    size_t total_count() const {
        size_t total = 0;
        for (const auto& [_, count] : frequencies) total += count;
        return total;
    }

    const std::unordered_map<std::string, size_t>& get_frequencies() const { return frequencies; }
};

// ==================== PARALLEL PROCESSOR ====================
class ParallelProcessor {
public:
    static void process_text_parallel(
        const std::string& text,
        size_t n,
        bool use_words,
        FrequencyCounter& counter
    ) {
        std::string normalized = Tokenizer::normalize(text, true);
        int num_threads = omp_get_max_threads();
        omp_set_num_threads(num_threads);

        size_t chunk_size = normalized.size() / num_threads;
        std::vector<size_t> chunk_starts(num_threads), chunk_ends(num_threads);
        for (int i = 0; i < num_threads; ++i) {
            size_t start = i * chunk_size;
            size_t end = (i == num_threads - 1) ? normalized.size() : (i + 1) * chunk_size;
            if (use_words && end < normalized.size()) while (end < normalized.size() && !std::isspace(normalized[end])) end++;
            chunk_starts[i] = start;
            chunk_ends[i] = end;
        }

        #pragma omp parallel
        {
            int thread_id = omp_get_thread_num();
            std::unordered_map<std::string, size_t> local_freq;
            std::string chunk = normalized.substr(chunk_starts[thread_id], chunk_ends[thread_id] - chunk_starts[thread_id]);

            if (use_words) {
                auto tokens = Tokenizer::tokenize_words(chunk);
                auto ngrams = NgramExtractor<std::string>::extract(tokens, n);
                for (const auto& ng : ngrams) local_freq[NgramExtractor<std::string>::ngram_to_string(ng)]++;
            } else {
                auto tokens = Tokenizer::tokenize_chars(chunk);
                auto ngrams = NgramExtractor<char>::extract(tokens, n);
                for (const auto& ng : ngrams) local_freq[NgramExtractor<char>::ngram_to_string(ng)]++;
            }

            counter.merge(local_freq);
        }
    }
};

// ==================== STATISTICS GENERATOR ====================
class StatisticsGenerator {
public:
    static void print_statistics(const FrequencyCounter& counter, const std::string& type, size_t n, size_t top_n = 20) {
        std::cout << "\n========== " << type << " (n=" << n << ") ==========\n";
        std::cout << "Total unique " << type << ": " << counter.total_unique() << "\n";
        std::cout << "Total count: " << counter.total_count() << "\n\n";
        std::cout << "Top " << top_n << " most frequent:\n";
        for (size_t i = 0; i < counter.get_top_n(top_n).size(); ++i) {
            auto [ngram, freq] = counter.get_top_n(top_n)[i];
            std::cout << (i + 1) << ". \"" << ngram << "\" - " << freq << " occurrences\n";
        }
    }

    static void save_to_file(const FrequencyCounter& counter, const std::string& filename) {
        std::ofstream out(filename);
        if (!out) { std::cerr << "Errore nell'aprire il file: " << filename << "\n"; return; }
        auto top = counter.get_top_n(counter.total_unique());
        out << "ngram,frequency\n";
        for (const auto& [ngram, freq] : top) out << "\"" << ngram << "\"," << freq << "\n";
        std::cout << "Statistiche salvate in: " << filename << "\n";
    }
};

// ==================== MAIN ====================
int main() {
    std::cout << "N-gram Analyzer with OpenMP – Ready-to-Use\n";
    std::cout << "===========================================\n\n";

    // Path automatico al libro
    std::string filepath = "/Users/lorenzocappetti/CLionProjects/Bigram/The Complete Harry Potter.txt";

    std::ifstream file(filepath);
    if (!file) {
        std::cerr << "Impossibile aprire il file: " << filepath << "\n";
        return 1;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string text = buffer.str();
    std::cout << "Caricato file: " << filepath << "\n";

    double start_time = omp_get_wtime();

    FrequencyCounter word_bigrams, word_trigrams, char_bigrams, char_trigrams;

    ParallelProcessor::process_text_parallel(text, 2, true, word_bigrams);
    ParallelProcessor::process_text_parallel(text, 3, true, word_trigrams);
    ParallelProcessor::process_text_parallel(text, 2, false, char_bigrams);
    ParallelProcessor::process_text_parallel(text, 3, false, char_trigrams);

    StatisticsGenerator::print_statistics(word_bigrams, "Word Bigrams", 2);
    StatisticsGenerator::print_statistics(word_trigrams, "Word Trigrams", 3);
    StatisticsGenerator::print_statistics(char_bigrams, "Char Bigrams", 2);
    StatisticsGenerator::print_statistics(char_trigrams, "Char Trigrams", 3);

    double end_time = omp_get_wtime();
    std::cout << "\nTempo totale di esecuzione: " << (end_time - start_time) << " secondi\n";

    StatisticsGenerator::save_to_file(word_bigrams, "word_bigrams.csv");
    StatisticsGenerator::save_to_file(word_trigrams, "word_trigrams.csv");
    StatisticsGenerator::save_to_file(char_bigrams, "char_bigrams.csv");
    StatisticsGenerator::save_to_file(char_trigrams, "char_trigrams.csv");

    return 0;
}
