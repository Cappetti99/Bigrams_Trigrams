//
// Lorenzo Cappetti, 2025 - OpenMP Version (Final Clean)
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
#include <omp.h>
#include <iomanip>
#include <cmath>

namespace fs = std::filesystem;

static void ensure_directory_exists(const std::string& dir) {
    if (!fs::exists(dir)) {
        fs::create_directories(dir);
    }
}

// ==================== PERFORMANCE PROFILER ====================
class Profiler {
    std::chrono::high_resolution_clock::time_point start;
    std::string name;
public:
    Profiler(const std::string& n) : name(n) {
        start = std::chrono::high_resolution_clock::now();
    }
    ~Profiler() {
        // Profiling silenzioso
    }
};

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

    static void tokenize_words(const std::string& text, std::vector<std::string>& tokens) {
        tokens.clear();
        tokens.reserve(text.size() / 6);
        std::istringstream iss(text);
        std::string word;
        while (iss >> word) {
            if (!word.empty()) tokens.push_back(std::move(word));
        }
    }

    static void tokenize_chars(const std::string& text, std::vector<char>& chars) {
        chars.clear();
        chars.reserve(text.size());
        for (char c : text) {
            if (!std::isspace(static_cast<unsigned char>(c))) {
                chars.push_back(c);
            }
        }
    }
};

// ==================== N-GRAM EXTRACTOR ====================
template<typename T>
class NgramExtractor {
public:
    static inline void extract_and_count(
        const std::vector<T>& tokens,
        size_t n,
        std::unordered_map<std::string, size_t>& freq_map
    ) {
        if (tokens.size() < n) return;
        freq_map.reserve(freq_map.size() + tokens.size() / 2);
        std::ostringstream oss;
        for (size_t i = 0; i <= tokens.size() - n; ++i) {
            oss.str("");
            oss.clear();
            oss << tokens[i];
            for (size_t j = 1; j < n; ++j) {
                oss << ' ' << tokens[i + j];
            }
            freq_map[oss.str()]++;
        }
    }
};

// ==================== FREQUENCY COUNTER ====================
class FrequencyCounter {
private:
    std::unordered_map<std::string, size_t> frequencies;

public:
    void merge(const std::unordered_map<std::string, size_t>& other) {
        if (frequencies.empty()) {
            frequencies = other;
        } else {
            frequencies.reserve(frequencies.size() + other.size());
            for (const auto& [ngram, count] : other) {
                frequencies[ngram] += count;
            }
        }
    }

    std::vector<std::pair<std::string, size_t>> get_top_n(size_t n) const {
        std::vector<std::pair<std::string, size_t>> sorted(frequencies.begin(), frequencies.end());
        if (n < sorted.size()) {
            std::partial_sort(
                sorted.begin(),
                sorted.begin() + n,
                sorted.end(),
                [](const auto& a, const auto& b) { return a.second > b.second; }
            );
            sorted.resize(n);
        } else {
            std::sort(sorted.begin(), sorted.end(),
                [](const auto& a, const auto& b) { return a.second > b.second; });
        }
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

// ==================== OPENMP PROCESSOR ====================
class OpenMPProcessor {
public:
    static void process_text_optimized(
        const std::string& text,
        std::unordered_map<std::string, size_t>& wb,
        std::unordered_map<std::string, size_t>& wt,
        std::unordered_map<std::string, size_t>& cb,
        std::unordered_map<std::string, size_t>& ct
    ) {
        std::string normalized = Tokenizer::normalize(text, true);

        std::vector<std::string> words;
        Tokenizer::tokenize_words(normalized, words);
        NgramExtractor<std::string>::extract_and_count(words, 2, wb);
        NgramExtractor<std::string>::extract_and_count(words, 3, wt);

        std::vector<char> chars;
        Tokenizer::tokenize_chars(normalized, chars);
        NgramExtractor<char>::extract_and_count(chars, 2, cb);
        NgramExtractor<char>::extract_and_count(chars, 3, ct);
    }

    static void parallel_merge(
        std::vector<std::unordered_map<std::string, size_t>>& thread_maps,
        FrequencyCounter& result,
        const std::string& type
    ) {
        Profiler prof("Merge " + type);
        int num_maps = thread_maps.size();
        while (num_maps > 1) {
            int next_num = (num_maps + 1) / 2;
            #pragma omp parallel for schedule(dynamic)
            for (int i = 0; i < num_maps / 2; ++i) {
                auto& map1 = thread_maps[i];
                auto& map2 = thread_maps[num_maps - 1 - i];
                map1.reserve(map1.size() + map2.size());
                for (const auto& [key, val] : map2) {
                    map1[key] += val;
                }
                map2.clear();
            }
            num_maps = next_num;
        }
        result.merge(thread_maps[0]);
    }

    static void process_parallel(
        const std::vector<std::string>& book_files,
        FrequencyCounter& word_bigrams,
        FrequencyCounter& word_trigrams,
        FrequencyCounter& char_bigrams,
        FrequencyCounter& char_trigrams,
        int num_threads = 0
    ) {
        if (num_threads == 0)
            num_threads = omp_get_max_threads();

        omp_set_num_threads(num_threads);
        std::cout << "Usando " << num_threads << " threads\n";

        int total_books = book_files.size();
        size_t total_bytes = 0;

        std::vector<std::unordered_map<std::string, size_t>> thread_wb(num_threads);
        std::vector<std::unordered_map<std::string, size_t>> thread_wt(num_threads);
        std::vector<std::unordered_map<std::string, size_t>> thread_cb(num_threads);
        std::vector<std::unordered_map<std::string, size_t>> thread_ct(num_threads);

        auto processing_start = std::chrono::high_resolution_clock::now();

        #pragma omp parallel reduction(+:total_bytes)
        {
            int tid = omp_get_thread_num();
            auto& my_wb = thread_wb[tid];
            auto& my_wt = thread_wt[tid];
            auto& my_cb = thread_cb[tid];
            auto& my_ct = thread_ct[tid];

            my_wb.reserve(100000);
            my_wt.reserve(200000);
            my_cb.reserve(50000);
            my_ct.reserve(100000);

            #pragma omp for schedule(dynamic) nowait
            for (int i = 0; i < total_books; ++i) {
                const auto& filepath = book_files[i];
                std::ifstream file(filepath, std::ios::binary | std::ios::ate);
                if (!file) {
                    #pragma omp critical
                    std::cerr << "❌ Impossibile aprire: " << filepath << "\n";
                    continue;
                }

                std::streamsize size = file.tellg();
                file.seekg(0, std::ios::beg);
                std::string text(size, '\0');
                if (!file.read(&text[0], size)) {
                    continue;
                }

                total_bytes += size;
                text = TextCleaner::clean_text(text);
                process_text_optimized(text, my_wb, my_wt, my_cb, my_ct);
            }
        }

        auto processing_end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> proc_time = processing_end - processing_start;

        std::cout << "✓ Processing completato in " << std::fixed << std::setprecision(2)
                  << proc_time.count() << "s\n\n";

        parallel_merge(thread_wb, word_bigrams, "Word Bigrams");
        parallel_merge(thread_wt, word_trigrams, "Word Trigrams");
        parallel_merge(thread_cb, char_bigrams, "Char Bigrams");
        parallel_merge(thread_ct, char_trigrams, "Char Trigrams");
    }
};

// ==================== STATISTICS GENERATOR ====================
class StatisticsGenerator {
public:
    static void print_statistics(const FrequencyCounter& counter,
                                const std::string& type,
                                size_t n,
                                size_t top_n = 20) {
        std::cout << "\n╔════════════════════════════════════════════════════╗\n";
        std::cout << "║ " << std::left << std::setw(50) << (type + " (n=" + std::to_string(n) + ")") << " ║\n";
        std::cout << "╠════════════════════════════════════════════════════╣\n";
        std::cout << "║ Total unique: " << std::setw(35) << counter.total_unique() << " ║\n";
        std::cout << "║ Total count:  " << std::setw(35) << counter.total_count() << " ║\n";
        std::cout << "╚════════════════════════════════════════════════════╝\n\n";

        std::cout << "Top " << top_n << " most frequent:\n";
        auto top = counter.get_top_n(top_n);
        for (size_t i = 0; i < top.size(); ++i) {
            auto [ngram, freq] = top[i];
            std::cout << std::setw(3) << (i + 1) << ". "
                      << std::left << std::setw(30) << ("\"" + ngram + "\"")
                      << std::right << std::setw(10) << freq << " occurrences\n";
        }
    }

    static void save_to_file(const FrequencyCounter& counter, const std::string& filename) {
        Profiler prof("Save " + filename);
        std::ofstream out(filename);
        if (!out) {
            std::cerr << "❌ Errore nell'aprire il file: " << filename << "\n";
            return;
        }
        auto top = counter.get_top_n(counter.total_unique());
        out << "ngram,frequency\n";
        for (const auto& [ngram, freq] : top) {
            out << "\"" << ngram << "\"," << freq << "\n";
        }
    }
};

// ==================== MAIN ====================
int main() {
    std::cout << "\n";
    std::cout << "╔═══════════════════════════════════════════════════════╗\n";
    std::cout << "║          OpenMP N-gram Analyzer (Parallel)            ║\n";
    std::cout << "║             Lorenzo Cappetti, 2025                    ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════╝\n\n";

    // Richiesta numero di thread
    int hw_threads = omp_get_max_threads();
    int num_threads = 0;
    // Permettiamo "virtual threads" fino a un limite ragionevole per testare l'oversubscription.
    int virtual_max = std::max(hw_threads * 4, 32);

    std::cout << "🧵 Thread logici disponibili sul sistema: " << hw_threads << "\n";
    std::cout << "Puoi scegliere fino a " << hw_threads << " thread fisici o usare thread virtuali fino a " << virtual_max << " (oversubscription).\n";
    std::cout << "Quanti thread vuoi utilizzare? (1-" << virtual_max << "): ";
    std::cin >> num_threads;

    // Validazione input: accettiamo numeri fino a virtual_max
    if (std::cin.fail() || num_threads < 1 || num_threads > virtual_max) {
        std::cin.clear();
        std::cin.ignore(10000, '\n');
        std::cout << "⚠️ Input non valido. Uso il massimo fisico (" << hw_threads << " threads)\n";
        num_threads = hw_threads;
    } else if (num_threads > hw_threads) {
        std::cout << "⚠️ Attenzione: stai usando " << num_threads << " thread (oversubscription). Il numero di thread fisici e' " << hw_threads << ".\n";
    }
    std::cout << "\n";

    std::string folder_path = "/Users/lorenzocappetti/CLionProjects/Bigrams_Trigrams/book_gutenberg";

    if (!fs::exists(folder_path) || !fs::is_directory(folder_path)) {
        std::cerr << "❌ Errore: cartella '" << folder_path << "' non trovata!\n";
        return 1;
    }

    std::vector<std::string> book_files;
    for (const auto& entry : fs::directory_iterator(folder_path)) {
        if (entry.path().extension() == ".txt")
            book_files.push_back(entry.path().string());
    }

    if (book_files.empty()) {
        std::cerr << "❌ Nessun file .txt trovato nella cartella!\n";
        return 1;
    }

    std::cout << "📚 Trovati " << book_files.size() << " libri da processare\n\n";

    // ==================== MULTIPLE RUN BENCHMARK ====================
    const int NUM_RUNS = 30;
    std::vector<double> run_times;
    run_times.reserve(NUM_RUNS);

    std::cout << "🔄 Eseguendo " << NUM_RUNS << " run per ottenere statistiche affidabili...\n\n";

    FrequencyCounter word_bigrams, word_trigrams, char_bigrams, char_trigrams;

    for (int run = 0; run < NUM_RUNS; ++run) {
        // Reset dei contatori per ogni run
        word_bigrams = FrequencyCounter();
        word_trigrams = FrequencyCounter();
        char_bigrams = FrequencyCounter();
        char_trigrams = FrequencyCounter();

        std::cout << "▶ Run " << std::setw(3) << (run + 1) << "/" << NUM_RUNS << " ... ";
        std::cout.flush();

        auto start_time = std::chrono::high_resolution_clock::now();

        OpenMPProcessor::process_parallel(
            book_files,
            word_bigrams, word_trigrams,
            char_bigrams, char_trigrams,
            num_threads
        );

        auto end_time = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = end_time - start_time;
        run_times.push_back(elapsed.count());

        std::cout << "completato in " << std::fixed << std::setprecision(2)
                  << elapsed.count() << "s\n";
    }

    // ==================== CALCOLO STATISTICHE ====================
    double mean = 0.0, min_time = run_times[0], max_time = run_times[0];
    for (double t : run_times) {
        mean += t;
        min_time = std::min(min_time, t);
        max_time = std::max(max_time, t);
    }
    mean /= run_times.size();

    double stddev = 0.0;
    for (double t : run_times) {
        stddev += (t - mean) * (t - mean);
    }
    stddev = std::sqrt(stddev / run_times.size());

    double cv = (stddev / mean) * 100.0; // Coefficiente di variazione in %

    // ==================== STAMPA STATISTICHE ====================
    std::cout << "\n";
    std::cout << "╔═══════════════════════════════════════════════════════╗\n";
    std::cout << "║          STATISTICHE PERFORMANCE (" << NUM_RUNS << " run)            ║\n";
    std::cout << "╠═══════════════════════════════════════════════════════╣\n";
    std::cout << "║ Media:              " << std::setw(30) << std::fixed << std::setprecision(3)
              << mean << "s ║\n";
    std::cout << "║ Minimo:             " << std::setw(30) << min_time << "s ║\n";
    std::cout << "║ Massimo:            " << std::setw(30) << max_time << "s ║\n";
    std::cout << "║ Deviazione Std:     " << std::setw(30) << stddev << "s ║\n";
    std::cout << "║ Coeff. Variazione:  " << std::setw(29) << std::setprecision(2)
              << cv << "% ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════╝\n";

    // Mostra statistiche degli n-gram (dall'ultimo run)
    StatisticsGenerator::print_statistics(word_bigrams, "Word Bigrams", 2);
    StatisticsGenerator::print_statistics(word_trigrams, "Word Trigrams", 3);
    StatisticsGenerator::print_statistics(char_bigrams, "Char Bigrams", 2);
    StatisticsGenerator::print_statistics(char_trigrams, "Char Trigrams", 3);

    // ==================== SALVATAGGIO RISULTATI ====================
    // Nota: Salviamo solo una volta dato che i risultati sono identici per ogni run.
    // Le 100 run servono solo per ottenere statistiche affidabili sui tempi.
    std::string output_dir = "output_parallel";
    ensure_directory_exists(output_dir);

    std::cout << "\n💾 Salvando risultati in " << output_dir << "/...\n";
    StatisticsGenerator::save_to_file(word_bigrams, output_dir + "/word_bigrams_par.csv");
    StatisticsGenerator::save_to_file(word_trigrams, output_dir + "/word_trigrams_par.csv");
    StatisticsGenerator::save_to_file(char_bigrams, output_dir + "/char_bigrams_par.csv");
    StatisticsGenerator::save_to_file(char_trigrams, output_dir + "/char_trigrams_par.csv");

    std::cout << "\n✓ Analisi completata con successo!\n";

    return 0;
}
