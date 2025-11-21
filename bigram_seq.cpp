//
// Lorenzo Cappetti, 2025 - Sequential Version (DEFINITIVAMENTE CORRETTO)
// IDENTICO al Tokenizer del test e delle versioni parallele
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

namespace fs = std::filesystem;

static void ensure_directory_exists(const std::string& dir) {
    if (!fs::exists(dir)) {
        fs::create_directories(dir);
    }
}

//═══════════════════════════════════════════════════════════════
// TEXT CLEANER
//═══════════════════════════════════════════════════════════════
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
        return cleaned;
    }
};

//═══════════════════════════════════════════════════════════════
// TOKENIZER - IDENTICO A QUELLO DEL TEST DI CORRETTEZZA
//═══════════════════════════════════════════════════════════════
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
};

//═══════════════════════════════════════════════════════════════
// OPTIMIZED STRING POOL
//═══════════════════════════════════════════════════════════════
class OptimizedStringPool {
private:
    std::vector<char> arena;
    std::vector<std::string_view> id_to_word;
    std::unordered_map<std::string_view, size_t> word_to_id;

public:
    OptimizedStringPool() {
        arena.reserve(10'000'000);
    }

    size_t intern(std::string_view s) {
        auto it = word_to_id.find(s);
        if (it != word_to_id.end()) {
            return it->second;
        }
        size_t pos = arena.size();
        arena.insert(arena.end(), s.begin(), s.end());
        std::string_view view(arena.data() + pos, s.size());
        size_t new_id = id_to_word.size();
        id_to_word.push_back(view);
        word_to_id[view] = new_id;
        return new_id;
    }

    std::string_view get(size_t id) const {
        return id_to_word[id];
    }

    size_t size() const { return id_to_word.size(); }
};

//═══════════════════════════════════════════════════════════════
// NGRAM ID
//═══════════════════════════════════════════════════════════════
struct NgramID {
    size_t word_ids[3];
    size_t length;

    bool operator==(const NgramID& other) const noexcept {
        if (length != other.length) return false;
        for (size_t i = 0; i < length; ++i) {
            if (word_ids[i] != other.word_ids[i]) return false;
        }
        return true;
    }
};

struct NgramIDHash {
    size_t operator()(const NgramID& n) const noexcept {
        size_t hash = n.length;
        for (size_t i = 0; i < n.length; ++i) {
            hash ^= (n.word_ids[i] * 2654435761ULL) + (hash << 6) + (hash >> 2);
        }
        return hash;
    }
};

//═══════════════════════════════════════════════════════════════
// HYBRID FREQUENCY COUNTER
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
            size_t start = 0;
            size_t pos = 0;

            while (pos <= ngram_str.size() && id.length < 3) {
                if (pos == ngram_str.size() || ngram_str[pos] == ' ') {
                    if (pos > start) {
                        std::string_view word(&ngram_str[start], pos - start);
                        id.word_ids[id.length++] = pool.intern(word);
                    }
                    start = pos + 1;
                }
                pos++;
            }
            ngram_ids.push_back(id);
            frequencies.push_back(freq);
        }
        finalized = true;
    }

    std::vector<std::pair<std::string, size_t>> get_top_n(size_t n) const {
        if (!finalized) {
            throw std::runtime_error("Devi chiamare build_from_aos() prima!");
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
            for (size_t j = 0; j < id.length; ++j) {
                if (j > 0) ngram += " ";
                ngram += std::string(pool.get(id.word_ids[j]));
            }
            result.emplace_back(std::move(ngram), frequencies[idx]);
        }
        return result;
    }

    std::vector<std::pair<std::string, size_t>> get_all_sorted() const {
        if (!finalized) {
            throw std::runtime_error("Devi chiamare build_from_aos() prima!");
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
            for (size_t j = 0; j < id.length; ++j) {
                if (j > 0) ngram += " ";
                ngram += std::string(pool.get(id.word_ids[j]));
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
// CSV SAVER
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
            std::cerr << "❌ Errore apertura file: " << filename << "\n";
            return;
        }
        out << "ngram,frequency\n";
        for (const auto& [ngram, freq] : all_ngrams) {
            out << "\"" << ngram << "\"," << freq << "\n";
        }
        out.close();
        std::cout << "💾 " << label << ": " << all_ngrams.size()
                  << " n-grams → " << filename << "\n";
    }
};

//═══════════════════════════════════════════════════════════════
// SEQUENTIAL PROCESSOR - IDENTICO AL TEST
//═══════════════════════════════════════════════════════════════
class SequentialProcessor {
public:
    static void process_text_aos_optimized(
        const std::string& text,
        std::unordered_map<std::string, size_t>& word_bigrams,
        std::unordered_map<std::string, size_t>& word_trigrams,
        std::unordered_map<std::string, size_t>& char_bigrams,
        std::unordered_map<std::string, size_t>& char_trigrams,
        std::vector<std::string>& words_buffer,
        std::vector<char>& chars_buffer
    ) {
        std::string normalized = Tokenizer::normalize(text, true);

        // WORD TOKENIZATION
        words_buffer.clear();
        std::istringstream iss(normalized);
        std::string word;
        while (iss >> word) {
            if (!word.empty()) {
                words_buffer.push_back(std::move(word));
            }
        }

        // WORD BIGRAMS
        for (size_t i = 0; i + 1 < words_buffer.size(); ++i) {
            std::string key = words_buffer[i] + " " + words_buffer[i + 1];
            word_bigrams[key]++;
        }

        // WORD TRIGRAMS
        for (size_t i = 0; i + 2 < words_buffer.size(); ++i) {
            std::string key = words_buffer[i] + " " + words_buffer[i + 1] + " " + words_buffer[i + 2];
            word_trigrams[key]++;
        }

        // CHAR TOKENIZATION
        chars_buffer.clear();
        for (char c : normalized) {
            if (!std::isspace(static_cast<unsigned char>(c))) {
                chars_buffer.push_back(c);
            }
        }

        // CHAR BIGRAMS
        std::string key;
        key.resize(3);
        for (size_t i = 0; i + 1 < chars_buffer.size(); ++i) {
            key[0] = chars_buffer[i];
            key[1] = ' ';
            key[2] = chars_buffer[i + 1];
            char_bigrams[key]++;
        }

        // CHAR TRIGRAMS
        key.resize(5);
        for (size_t i = 0; i + 2 < chars_buffer.size(); ++i) {
            key[0] = chars_buffer[i];
            key[1] = ' ';
            key[2] = chars_buffer[i + 1];
            key[3] = ' ';
            key[4] = chars_buffer[i + 2];
            char_trigrams[key]++;
        }
    }

    static void process_sequential(
        const std::vector<std::string>& book_files,
        HybridFrequencyCounter& word_bigrams,
        HybridFrequencyCounter& word_trigrams,
        HybridFrequencyCounter& char_bigrams,
        HybridFrequencyCounter& char_trigrams
    ) {
        std::unordered_map<std::string, size_t> wb_map, wt_map, cb_map, ct_map;
        wb_map.reserve(100000);
        wt_map.reserve(200000);
        cb_map.reserve(50000);
        ct_map.reserve(100000);

        std::vector<std::string> words_buf;
        std::vector<char> chars_buf;
        words_buf.reserve(10000);
        chars_buf.reserve(50000);

        int total_books = book_files.size();
        int progress = 0;

        for (const auto& filepath : book_files) {
            std::ifstream file(filepath, std::ios::binary | std::ios::ate);
            if (!file) {
                std::cerr << "⚠️  Impossibile aprire: " << filepath << "\n";
                continue;
            }
            std::streamsize size = file.tellg();
            file.seekg(0, std::ios::beg);
            std::string text(size, '\0');
            if (!file.read(&text[0], size)) continue;

            text = TextCleaner::clean_text(text);
            process_text_aos_optimized(text, wb_map, wt_map, cb_map, ct_map,
                                      words_buf, chars_buf);

            progress++;
            if (progress % 10 == 0 || progress == total_books) {
                std::cout << "\r📖 Processati: " << progress << "/" << total_books
                          << " libri (" << std::fixed << std::setprecision(1)
                          << (100.0 * progress / total_books) << "%)   " << std::flush;
            }
        }
        std::cout << "\n";

        std::cout << "🔄 Conversione AoS → SoA...\n";
        word_bigrams.build_from_aos(wb_map);
        word_trigrams.build_from_aos(wt_map);
        char_bigrams.build_from_aos(cb_map);
        char_trigrams.build_from_aos(ct_map);
    }
};

//═══════════════════════════════════════════════════════════════
// MAIN
//═══════════════════════════════════════════════════════════════
int main() {
    std::cout << "\n╔═══════════════════════════════════════════════════════╗\n";
    std::cout << "║   Sequential N-gram Analyzer (Hybrid AoS/SoA)        ║\n";
    std::cout << "║              Lorenzo Cappetti, 2025                   ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════╝\n\n";

    std::string folder_path = "/Users/lorenzocappetti/CLionProjects/Bigrams_Trigrams/book_gutenberg";

    if (!fs::exists(folder_path)) {
        std::cerr << "❌ Cartella non trovata: " << folder_path << "\n";
        return 1;
    }

    std::vector<std::string> book_files;
    for (const auto& entry : fs::directory_iterator(folder_path)) {
        if (entry.path().extension() == ".txt") {
            book_files.push_back(entry.path().string());
        }
    }

    if (book_files.empty()) {
        std::cerr << "❌ Nessun file .txt trovato!\n";
        return 1;
    }

    std::cout << "📚 Trovati " << book_files.size() << " libri\n\n";

    const int WARMUP_RUNS = 2;
    const int MEASURED_RUNS = 10;
    const int NUM_RUNS = WARMUP_RUNS + MEASURED_RUNS;

    struct RunResult { double wall; double cpu; bool warmup; };
    std::vector<RunResult> run_times;
    run_times.reserve(NUM_RUNS);

    std::cout << "🔄 Eseguendo " << NUM_RUNS << " run totali (" << WARMUP_RUNS
              << " warm-up + " << MEASURED_RUNS << " misurate)...\n\n";

    HybridFrequencyCounter word_bigrams, word_trigrams, char_bigrams, char_trigrams;

    for (int run = 0; run < NUM_RUNS; ++run) {
        word_bigrams = HybridFrequencyCounter();
        word_trigrams = HybridFrequencyCounter();
        char_bigrams = HybridFrequencyCounter();
        char_trigrams = HybridFrequencyCounter();

        auto start_wall = std::chrono::high_resolution_clock::now();
        std::clock_t start_cpu = std::clock();

        SequentialProcessor::process_sequential(
            book_files,
            word_bigrams,
            word_trigrams,
            char_bigrams,
            char_trigrams
        );

        auto end_wall = std::chrono::high_resolution_clock::now();
        std::clock_t end_cpu = std::clock();

        std::chrono::duration<double> elapsed = end_wall - start_wall;
        double cpu_seconds = double(end_cpu - start_cpu) / double(CLOCKS_PER_SEC);

        run_times.push_back(RunResult{elapsed.count(), cpu_seconds, run < WARMUP_RUNS});

        std::cout << "  Run " << std::setw(2) << (run + 1) << "/" << NUM_RUNS;
        if (run < WARMUP_RUNS) {
            std::cout << " [WARM-UP]: ";
        } else {
            std::cout << ": ";
        }
        std::cout << std::fixed << std::setprecision(2) << elapsed.count()
                  << "s (cpu: " << std::setprecision(2) << cpu_seconds << "s)\n";
    }

    std::vector<double> measured_wall, measured_cpu;
    for (const auto& r : run_times) {
        if (!r.warmup) {
            measured_wall.push_back(r.wall);
            measured_cpu.push_back(r.cpu);
        }
    }

    double mean_wall = 0.0, min_wall = measured_wall[0], max_wall = measured_wall[0];
    for (double t : measured_wall) {
        mean_wall += t;
        min_wall = std::min(min_wall, t);
        max_wall = std::max(max_wall, t);
    }
    mean_wall /= measured_wall.size();

    double stddev_wall = 0.0;
    for (double t : measured_wall) {
        stddev_wall += (t - mean_wall) * (t - mean_wall);
    }
    stddev_wall = std::sqrt(stddev_wall / measured_wall.size());
    double cv_wall = (stddev_wall / mean_wall) * 100.0;

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

    std::string output_dir = "test/output_sequential";
    ensure_directory_exists(output_dir);

    std::string perf_report = output_dir + "/performance_report_seq.txt";
    std::ofstream report(perf_report);
    if (report) {
        report << "═══════════════════════════════════════════════════════════════\n";
        report << "  PERFORMANCE METRICS REPORT - SEQUENTIAL N-GRAM ANALYZER\n";
        report << "  Lorenzo Cappetti, 2025\n";
        report << "  Generato: " << __DATE__ << " " << __TIME__ << "\n";
        report << "═══════════════════════════════════════════════════════════════\n\n";

        report << "1. CONFIGURAZIONE ESECUZIONE\n";
        report << "   - Numero libri processati:  " << book_files.size() << "\n";
        report << "   - Thread utilizzati:        1 (sequenziale)\n";
        report << "   - Run totali:               " << NUM_RUNS << "\n";
        report << "   - Run warm-up (scartate):   " << WARMUP_RUNS << "\n";
        report << "   - Run misurate:             " << MEASURED_RUNS << "\n\n";

        report << "2. EXECUTION TIME METRICS (Wall-Clock)\n";
        report << "   ┌─────────────────────────────────────────────────┐\n";
        report << "   │ Media:           " << std::setw(10) << std::fixed << std::setprecision(3)
               << mean_wall << " s                   │\n";
        report << "   │ Minimo:          " << std::setw(10) << min_wall << " s                   │\n";
        report << "   │ Massimo:         " << std::setw(10) << max_wall << " s                   │\n";
        report << "   │ Deviazione Std:  " << std::setw(10) << stddev_wall << " s                   │\n";
        report << "   │ Coeff. Variaz.:  " << std::setw(9) << std::setprecision(2)
               << cv_wall << " %                    │\n";
        report << "   └─────────────────────────────────────────────────┘\n\n";

        report << "3. CPU TIME METRICS\n";
        report << "   ┌─────────────────────────────────────────────────┐\n";
        report << "   │ Media:           " << std::setw(10) << std::fixed << std::setprecision(3)
               << mean_cpu << " s                   │\n";
        report << "   │ Minimo:          " << std::setw(10) << min_cpu << " s                   │\n";
        report << "   │ Massimo:         " << std::setw(10) << max_cpu << " s                   │\n";
        report << "   │ Deviazione Std:  " << std::setw(10) << stddev_cpu << " s                   │\n";
        report << "   │ Coeff. Variaz.:  " << std::setw(9) << std::setprecision(2)
               << cv_cpu << " %                    │\n";
        report << "   └─────────────────────────────────────────────────┘\n\n";

        report << "4. DETTAGLIO RUN INDIVIDUALI\n";
        report << "   Run  Warmup  Wall-Time(s)  CPU-Time(s)  Delta(%)\n";
        report << "   ───  ──────  ────────────  ───────────  ────────\n";
        for (size_t i = 0; i < run_times.size(); ++i) {
            double delta = (run_times[i].wall - run_times[i].cpu) / run_times[i].wall * 100.0;
            report << "   " << std::setw(3) << (i+1) << "    "
                   << (run_times[i].warmup ? "YES" : " NO") << "    "
                   << std::setw(10) << std::fixed << std::setprecision(3) << run_times[i].wall << "    "
                   << std::setw(10) << run_times[i].cpu << "    "
                   << std::setw(6) << std::setprecision(1) << delta << "\n";
        }
        report << "\n";

        report << "5. STABILITÀ E AFFIDABILITÀ\n";
        report << "   - Variabilità wall-clock:  ";
        if (cv_wall < 2.0) report << "ECCELLENTE (< 2%)\n";
        else if (cv_wall < 5.0) report << "BUONA (< 5%)\n";
        else if (cv_wall < 10.0) report << "ACCETTABILE (< 10%)\n";
        else report << "ALTA (≥ 10%)\n";

        report << "   - Variabilità CPU-time:    ";
        if (cv_cpu < 2.0) report << "ECCELLENTE (< 2%)\n";
        else if (cv_cpu < 5.0) report << "BUONA (< 5%)\n";
        else if (cv_cpu < 10.0) report << "ACCETTABILE (< 10%)\n";
        else report << "ALTA (≥ 10%)\n\n";

        report << "6. RISULTATI N-GRAM\n";
        report << "   - Word Bigrams:   " << word_bigrams.total_unique() << " unique\n";
        report << "   - Word Trigrams:  " << word_trigrams.total_unique() << " unique\n";
        report << "   - Char Bigrams:   " << char_bigrams.total_unique() << " unique\n";
        report << "   - Char Trigrams:  " << char_trigrams.total_unique() << " unique\n\n";

        report << "7. NOTE\n";
        report << "   - Le prime " << WARMUP_RUNS << " run sono state scartate per warm-up CPU/cache\n";
        report << "   - Tutte le statistiche sono calcolate solo sulle " << MEASURED_RUNS << " run misurate\n";
        report << "   - Delta(%) = differenza percentuale tra wall-time e CPU-time\n";
        report << "   - Un Delta elevato indica overhead I/O o sistema\n\n";

        report << "═══════════════════════════════════════════════════════════════\n";
        report.close();
        std::cout << "📊 Report performance salvato: " << perf_report << "\n";
    } else {
        std::cerr << "⚠️  Impossibile salvare report performance\n";
    }

    std::cout << "\n╔═══════════════════════════════════════════════════════╗\n";
    std::cout << "║    STATISTICHE PERFORMANCE (" << MEASURED_RUNS << " run misurate)       ║\n";
    std::cout << "╠═══════════════════════════════════════════════════════╣\n";
    std::cout << "║ WALL-CLOCK TIME                                       ║\n";
    std::cout << "║   Media:          " << std::setw(10) << std::fixed << std::setprecision(3)
              << mean_wall << " s                        ║\n";
    std::cout << "║   Minimo:         " << std::setw(10) << min_wall << " s                        ║\n";
    std::cout << "║   Massimo:        " << std::setw(10) << max_wall << " s                        ║\n";
    std::cout << "║   Dev. Standard:  " << std::setw(10) << stddev_wall << " s                        ║\n";
    std::cout << "║   Coeff. Variaz.: " << std::setw(9) << std::setprecision(2) << cv_wall << " %                         ║\n";
    std::cout << "║                                                       ║\n";
    std::cout << "║ CPU TIME                                              ║\n";
    std::cout << "║   Media:          " << std::setw(10) << std::fixed << std::setprecision(3)
              << mean_cpu << " s                        ║\n";
    std::cout << "║   Minimo:         " << std::setw(10) << min_cpu << " s                        ║\n";
    std::cout << "║   Massimo:        " << std::setw(10) << max_cpu << " s                        ║\n";
    std::cout << "║   Dev. Standard:  " << std::setw(10) << stddev_cpu << " s                        ║\n";
    std::cout << "║   Coeff. Variaz.: " << std::setw(9) << std::setprecision(2) << cv_cpu << " %                         ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════╝\n";

    // Mostra risultati nel formato che il test si aspetta
    auto top_wb = word_bigrams.get_top_n(20);
    auto top_wt = word_trigrams.get_top_n(20);
    auto top_cb = char_bigrams.get_top_n(20);
    auto top_ct = char_trigrams.get_top_n(20);

    std::cout << "\n╔════════════════════════════════════════════════════╗\n";
    std::cout << "║ " << std::left << std::setw(50) << "Word Bigrams (n=2)" << " ║\n";
    std::cout << "╠════════════════════════════════════════════════════╣\n";
    std::cout << "║ Total unique: " << std::setw(35) << word_bigrams.total_unique() << " ║\n";
    std::cout << "║ Total count:  " << std::setw(35) << word_bigrams.total_count() << " ║\n";
    std::cout << "╚════════════════════════════════════════════════════╝\n\n";

    std::cout << "Top 20 most frequent:\n";
    for (size_t i = 0; i < top_wb.size(); ++i) {
        std::cout << std::setw(3) << (i + 1) << ". "
                  << std::left << std::setw(30) << ("\"" + top_wb[i].first + "\"")
                  << std::right << std::setw(10) << top_wb[i].second << " occurrences\n";
    }

    std::cout << "\n╔════════════════════════════════════════════════════╗\n";
    std::cout << "║ " << std::left << std::setw(50) << "Word Trigrams (n=3)" << " ║\n";
    std::cout << "╠════════════════════════════════════════════════════╣\n";
    std::cout << "║ Total unique: " << std::setw(35) << word_trigrams.total_unique() << " ║\n";
    std::cout << "║ Total count:  " << std::setw(35) << word_trigrams.total_count() << " ║\n";
    std::cout << "╚════════════════════════════════════════════════════╝\n\n";

    std::cout << "Top 20 most frequent:\n";
    for (size_t i = 0; i < top_wt.size(); ++i) {
        std::cout << std::setw(3) << (i + 1) << ". "
                  << std::left << std::setw(30) << ("\"" + top_wt[i].first + "\"")
                  << std::right << std::setw(10) << top_wt[i].second << " occurrences\n";
    }

    std::cout << "\n╔════════════════════════════════════════════════════╗\n";
    std::cout << "║ " << std::left << std::setw(50) << "Char Bigrams (n=2)" << " ║\n";
    std::cout << "╠════════════════════════════════════════════════════╣\n";
    std::cout << "║ Total unique: " << std::setw(35) << char_bigrams.total_unique() << " ║\n";
    std::cout << "║ Total count:  " << std::setw(35) << char_bigrams.total_count() << " ║\n";
    std::cout << "╚════════════════════════════════════════════════════╝\n\n";

    std::cout << "Top 20 most frequent:\n";
    for (size_t i = 0; i < top_cb.size(); ++i) {
        std::cout << std::setw(3) << (i + 1) << ". "
                  << std::left << std::setw(30) << ("\"" + top_cb[i].first + "\"")
                  << std::right << std::setw(10) << top_cb[i].second << " occurrences\n";
    }

    std::cout << "\n╔════════════════════════════════════════════════════╗\n";
    std::cout << "║ " << std::left << std::setw(50) << "Char Trigrams (n=3)" << " ║\n";
    std::cout << "╠════════════════════════════════════════════════════╣\n";
    std::cout << "║ Total unique: " << std::setw(35) << char_trigrams.total_unique() << " ║\n";
    std::cout << "║ Total count:  " << std::setw(35) << char_trigrams.total_count() << " ║\n";
    std::cout << "╚════════════════════════════════════════════════════╝\n\n";

    std::cout << "Top 20 most frequent:\n";
    for (size_t i = 0; i < top_ct.size(); ++i) {
        std::cout << std::setw(3) << (i + 1) << ". "
                  << std::left << std::setw(30) << ("\"" + top_ct[i].first + "\"")
                  << std::right << std::setw(10) << top_ct[i].second << " occurrences\n";
    }

    std::cout << "\n💾 Salvando risultati in " << output_dir << "/\n";

    CSVSaver::save_ngrams(word_bigrams, output_dir + "/word_bigrams.csv", "Word Bigrams");
    CSVSaver::save_ngrams(word_trigrams, output_dir + "/word_trigrams.csv", "Word Trigrams");
    CSVSaver::save_ngrams(char_bigrams, output_dir + "/char_bigrams.csv", "Char Bigrams");
    CSVSaver::save_ngrams(char_trigrams, output_dir + "/char_trigrams.csv", "Char Trigrams");

    std::cout << "\n✅ Completato!\n\n";
    return 0;
}