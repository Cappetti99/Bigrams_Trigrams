//
// Lorenzo Cappetti, 2025 - Sequential Version (Updated Normalization)
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
#include <iomanip>
#include <cmath>

namespace fs = std::filesystem;

// Helper per creare directory se non esiste
static void ensure_directory_exists(const std::string& dir) {
    if (!fs::exists(dir)) {
        fs::create_directories(dir);
    }
}

// ==================== TEXT CLEANER ====================
class TextCleaner {
public:
    static std::string remove_gutenberg_header(const std::string& text) {
        size_t start_pos = text.find("*** START OF");
        if (start_pos != std::string::npos) {
            size_t end_of_line = text.find("***", start_pos + 12);
            if (end_of_line != std::string::npos) {
                return text.substr(end_of_line + 3);
            }
        }
        return text;
    }

    static std::string remove_gutenberg_footer(const std::string& text) {
        size_t end_pos = text.find("*** END OF");
        if (end_pos != std::string::npos) {
            return text.substr(0, end_pos);
        }
        return text;
    }

    static std::string clean_text(const std::string& text) {
        std::string cleaned = remove_gutenberg_header(text);
        cleaned = remove_gutenberg_footer(cleaned);
        size_t contents_pos = cleaned.find("Contents");
        if (contents_pos != std::string::npos && contents_pos < 5000) {
            size_t chapter_pos = cleaned.find("CHAPTER", contents_pos);
            if (chapter_pos != std::string::npos) {
                cleaned = cleaned.substr(0, contents_pos) + cleaned.substr(chapter_pos);
            }
        }
        return cleaned;
    }
};

// ==================== TOKENIZER ====================
class Tokenizer {
private:
    static const unsigned char* get_to_lower() {
        static unsigned char table[256];
        static bool initialized = false;
        if (!initialized) {
            for (int i = 0; i < 256; i++) {
                table[i] = (i >= 'A' && i <= 'Z') ? i + 32 : i;
            }
            initialized = true;
        }
        return table;
    }

    static inline std::string process_utf8_char(const unsigned char* bytes, size_t& skip) {
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
                if (second == 0x9D || second == 0xBD || second == 0x9F || second == 0xBF) return "y";
            }
            return "";
        }
        if ((bytes[0] & 0xF0) == 0xE0 && bytes[1] && bytes[2]) {
            skip = 3;
            if (bytes[0] == 0xE2 && bytes[1] == 0x80 && (bytes[2] >= 0x98 && bytes[2] <= 0x9F))
                return " ";
            return "";
        }
        if ((bytes[0] & 0xF8) == 0xF0 && bytes[1] && bytes[2] && bytes[3]) {
            skip = 4;
            return "";
        }
        return "";
    }

public:
    // ✅ Normalizzazione identica alla versione parallela
    static std::string normalize(const std::string& text, bool remove_punct = false) {
        std::string result;
        result.reserve(text.size());
        const unsigned char* to_lower = get_to_lower();
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
            if (std::isalpha(c)) {
                result += to_lower[c];
            } else if (std::isspace(c)) {
                result += ' ';
            }
        }
        return result;
    }

    static std::vector<std::string> tokenize_words(const std::string& text) {
        std::vector<std::string> tokens;
        tokens.reserve(text.size() / 6);
        std::istringstream iss(text);
        std::string word;
        while (iss >> word) {
            if (!word.empty()) tokens.push_back(word);
        }
        return tokens;
    }

    static std::vector<char> tokenize_chars(const std::string& text) {
        std::vector<char> chars;
        chars.reserve(text.size());
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
        ngrams.reserve(tokens.size() - n + 1);
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

public:
    void add_ngram(const std::string& ngram) {
        frequencies[ngram]++;
    }

    void merge(const std::unordered_map<std::string, size_t>& other) {
        for (const auto& [ngram, count] : other) {
            frequencies[ngram] += count;
        }
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

    const std::unordered_map<std::string, size_t>& get_frequencies() const {
        return frequencies;
    }
};

// ==================== SEQUENTIAL PROCESSOR ====================
class SequentialProcessor {
public:
    static void process_text_sequential(
        const std::string& text,
        size_t n,
        bool use_words,
        FrequencyCounter& counter
    ) {
        std::string normalized = Tokenizer::normalize(text, true);
        if (use_words) {
            auto tokens = Tokenizer::tokenize_words(normalized);
            auto ngrams = NgramExtractor<std::string>::extract(tokens, n);
            for (const auto& ng : ngrams)
                counter.add_ngram(NgramExtractor<std::string>::ngram_to_string(ng));
        } else {
            auto tokens = Tokenizer::tokenize_chars(normalized);
            auto ngrams = NgramExtractor<char>::extract(tokens, n);
            for (const auto& ng : ngrams)
                counter.add_ngram(NgramExtractor<char>::ngram_to_string(ng));
        }
    }
};

// ==================== STATISTICS GENERATOR ====================
class StatisticsGenerator {
public:
    static void print_statistics(const FrequencyCounter& counter,
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

    static void save_to_file(const FrequencyCounter& counter, const std::string& filename) {
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
    std::cout << "Sequential N-gram Analyzer for Multiple Books\n";
    std::cout << "==============================================\n\n";

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

    std::cout << "Trovati " << book_files.size() << " libri da processare\n\n";

    // ==================== MULTIPLE RUN BENCHMARK ====================
    const int WARMUP_RUNS = 2;  // Prime run da scartare per warm-up CPU/cache
    const int MEASURED_RUNS = 10;  // Run effettive da misurare
    const int NUM_RUNS = WARMUP_RUNS + MEASURED_RUNS;  // Totale: 12 run
    std::vector<double> run_times;
    run_times.reserve(NUM_RUNS);

    std::cout << "🔄 Eseguendo " << NUM_RUNS << " run totali (" << WARMUP_RUNS
              << " warm-up + " << MEASURED_RUNS
              << " misurate) per ottenere statistiche affidabili...\n\n";

    FrequencyCounter word_bigrams, word_trigrams, char_bigrams, char_trigrams;

    for (int run = 0; run < NUM_RUNS; ++run) {
        // Reset dei contatori per ogni run
        word_bigrams = FrequencyCounter();
        word_trigrams = FrequencyCounter();
        char_bigrams = FrequencyCounter();
        char_trigrams = FrequencyCounter();

        std::cout << "▶ Run " << std::setw(3) << (run + 1) << "/" << NUM_RUNS;
        if (run < WARMUP_RUNS) {
            std::cout << " [WARM-UP] ... ";
        } else {
            std::cout << " ... ";
        }
        std::cout.flush();

        auto start_time = std::chrono::high_resolution_clock::now();

        for (size_t i = 0; i < book_files.size(); ++i) {
            const auto& filepath = book_files[i];

            // Lettura identica alla versione parallela (binaria)
            std::ifstream file(filepath, std::ios::binary | std::ios::ate);
            if (!file) {
                continue;
            }

            std::streamsize size = file.tellg();
            file.seekg(0, std::ios::beg);
            std::string text(size, '\0');
            if (!file.read(&text[0], size)) {
                continue;
            }

            text = TextCleaner::clean_text(text);

            SequentialProcessor::process_text_sequential(text, 2, true, word_bigrams);
            SequentialProcessor::process_text_sequential(text, 3, true, word_trigrams);
            SequentialProcessor::process_text_sequential(text, 2, false, char_bigrams);
            SequentialProcessor::process_text_sequential(text, 3, false, char_trigrams);
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = end_time - start_time;
        run_times.push_back(elapsed.count());

        std::cout << "completato in " << std::fixed << std::setprecision(2)
                  << elapsed.count() << "s\n";
    }

    // ==================== CALCOLO STATISTICHE (SCARTANDO WARM-UP) ====================
    // Scarta le prime WARMUP_RUNS run per stabilizzare CPU/cache
    std::vector<double> measured_times(run_times.begin() + WARMUP_RUNS, run_times.end());

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

    double cv = (stddev / mean) * 100.0; // Coefficiente di variazione in %

    // ==================== STAMPA STATISTICHE ====================
    std::cout << "\n";
    std::cout << "╔═══════════════════════════════════════════════════════╗\n";
    std::cout << "║    STATISTICHE PERFORMANCE (" << (NUM_RUNS - WARMUP_RUNS) << " run misurate)     ║\n";
    std::cout << "╠═══════════════════════════════════════════════════════╣\n";
    std::cout << "║ Media:              " << std::setw(30) << std::fixed << std::setprecision(3)
              << mean << "s ║\n";
    std::cout << "║ Minimo:             " << std::setw(30) << min_time << "s ║\n";
    std::cout << "║ Massimo:            " << std::setw(30) << max_time << "s ║\n";
    std::cout << "║ Deviazione Std:     " << std::setw(30) << stddev << "s ║\n";
    std::cout << "║ Coeff. Variazione:  " << std::setw(29) << std::setprecision(2)
              << cv << "% ║\n";
    std::cout << "║                                                       ║\n";
    std::cout << "║ Note: Scartate " << WARMUP_RUNS << " run di warm-up iniziali          ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════╝\n";

    // Mostra statistiche degli n-gram (dall'ultimo run)
    StatisticsGenerator::print_statistics(word_bigrams, "Word Bigrams", 2);
    StatisticsGenerator::print_statistics(word_trigrams, "Word Trigrams", 3);
    StatisticsGenerator::print_statistics(char_bigrams, "Char Bigrams", 2);
    StatisticsGenerator::print_statistics(char_trigrams, "Char Trigrams", 3);

    // ==================== SALVATAGGIO RISULTATI ====================
    // Nota: Salviamo solo una volta dato che i risultati sono identici per ogni run.
    // Le 100 run servono solo per ottenere statistiche affidabili sui tempi.
    std::string output_dir = "test/output_sequential";
    ensure_directory_exists(output_dir);

    std::cout << "\n💾 Salvando risultati in " << output_dir << "/...\n";
    StatisticsGenerator::save_to_file(word_bigrams, output_dir + "/word_bigrams_seq.csv");
    StatisticsGenerator::save_to_file(word_trigrams, output_dir + "/word_trigrams_seq.csv");
    StatisticsGenerator::save_to_file(char_bigrams, output_dir + "/char_bigrams_seq.csv");
    StatisticsGenerator::save_to_file(char_trigrams, output_dir + "/char_trigrams_seq.csv");

    std::cout << "\n✓ Analisi completata con successo!\n";

    return 0;
}
