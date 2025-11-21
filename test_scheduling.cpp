//
// Test Scheduling Strategies - Lorenzo Cappetti, 2025
// Testa automaticamente tutte le strategie di scheduling OpenMP con 8 thread, utilizza solo AoS per testare gli scheduler
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
#include <cmath>

namespace fs = std::filesystem;

static void ensure_directory_exists(const std::string& dir) {
    if (!fs::exists(dir)) {
        fs::create_directories(dir);
    }
}

// ==================== TEXT CLEANER ====================
// Rimuove header/footer di Project Gutenberg dai testi
class TextCleaner {
public:
    // Rimuove l'header standard di Gutenberg (*** START OF...)
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

    // Rimuove il footer standard di Gutenberg (*** END OF...)
    static std::string remove_gutenberg_footer(const std::string& text) {
        size_t end_pos = text.find("*** END OF");
        if (end_pos != std::string::npos) {
            return text.substr(0, end_pos);
        }
        return text;
    }

    // Applica entrambe le pulizie
    static std::string clean_text(const std::string& text) {
        std::string cleaned = remove_gutenberg_header(text);
        cleaned = remove_gutenberg_footer(cleaned);
        return cleaned;
    }
};

// ==================== TOKENIZER ====================
// Normalizza testo e lo tokenizza in parole o caratteri
class Tokenizer {
private:
    // Lookup table ottimizzata per conversione a minuscolo (2x più veloce di std::tolower)
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

    // Gestisce caratteri UTF-8 (accenti) e li converte in ASCII base
    static inline std::string process_utf8_char(const unsigned char* bytes, size_t& skip) {
        skip = 0;

        // Solo caratteri a 2 byte (accenti: à, é, ñ, ecc.)
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

        // Skippa tutto il resto (emoji, simboli strani)
        if (bytes[0] >= 0x80) {
            skip = 1;
            while (skip < 4 && bytes[skip] && (bytes[skip] & 0xC0) == 0x80) skip++;
            return "";
        }

        return "";
    }

public:
    // Normalizza il testo: lowercase, rimuove numeri, gestisce UTF-8
    static std::string normalize(const std::string& text, bool remove_punct = false) {
        std::string result;
        result.reserve(text.size());
        const unsigned char* to_lower = get_to_lower();

        for (size_t i = 0; i < text.size(); ++i) {
            unsigned char c = static_cast<unsigned char>(text[i]);

            // Gestione UTF-8 (accenti)
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

            // Rimuovi numeri
            if (std::isdigit(c)) continue;

            // Gestione punteggiatura
            if (remove_punct && std::ispunct(c)) {
                result += ' ';
                continue;
            }

            // Conversione a minuscolo con lookup table
            if (std::isalpha(c)) {
                result += to_lower[c];
            } else if (std::isspace(c)) {
                result += ' ';
            }
        }
        return result;
    }

    // Tokenizza in parole (split su whitespace)
    static void tokenize_words(const std::string& text, std::vector<std::string>& tokens) {
        tokens.clear();
        tokens.reserve(text.size() / 6);  // Stima: 6 char per parola media
        std::istringstream iss(text);
        std::string word;
        while (iss >> word) {
            if (!word.empty()) tokens.push_back(std::move(word));
        }
    }

    // Tokenizza in caratteri (rimuove solo whitespace)
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
// Template generico per estrarre n-gram da sequenze di token
template<typename T>
class NgramExtractor {
public:
    // Estrae n-gram e conta le frequenze inline (sliding window)
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
// Gestisce mappe di frequenza e operazioni di merge/ordinamento
class FrequencyCounter {
private:
    std::unordered_map<std::string, size_t> frequencies;

public:
    // Merge di un'altra mappa nella corrente
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

    // Ritorna i top N n-gram ordinati per frequenza
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
};

// ==================== OPENMP PROCESSOR CON SCHEDULING PARAMETRIZZATO ====================
class OpenMPProcessor {
public:
    /**
     * Processa un singolo testo ed estrae tutti i tipi di n-gram.
     * Versione ottimizzata con concatenazione diretta invece di ostringstream.
     */
    static void process_text_aos_optimized(
        const std::string& text,
        std::unordered_map<std::string, size_t>& word_bigrams,
        std::unordered_map<std::string, size_t>& word_trigrams,
        std::unordered_map<std::string, size_t>& char_bigrams,
        std::unordered_map<std::string, size_t>& char_trigrams,
        std::vector<std::string>& words_buffer,
        std::vector<char>& chars_buffer
    ) {
        // Normalizza: lowercase, rimuove numeri e punteggiatura
        std::string normalized = Tokenizer::normalize(text, true);

        // Tokenizza in parole
        words_buffer.clear();
        Tokenizer::tokenize_words(normalized, words_buffer);

        std::string key;  // Buffer riutilizzabile per le chiavi

        // === WORD BIGRAMS ===
        for (size_t i = 0; i + 1 < words_buffer.size(); ++i) {
            const auto& w1 = words_buffer[i];
            const auto& w2 = words_buffer[i + 1];

            // Concatenazione diretta (più veloce di ostringstream)
            key.clear();
            key.reserve(w1.size() + w2.size() + 1);
            key.append(w1);
            key.push_back(' ');
            key.append(w2);
            word_bigrams[key]++;
        }

        // === WORD TRIGRAMS ===
        for (size_t i = 0; i + 2 < words_buffer.size(); ++i) {
            const auto& w1 = words_buffer[i];
            const auto& w2 = words_buffer[i + 1];
            const auto& w3 = words_buffer[i + 2];

            key.clear();
            key.reserve(w1.size() + w2.size() + w3.size() + 2);
            key.append(w1);
            key.push_back(' ');
            key.append(w2);
            key.push_back(' ');
            key.append(w3);
            word_trigrams[key]++;
        }

        // Tokenizza in caratteri
        chars_buffer.clear();
        Tokenizer::tokenize_chars(normalized, chars_buffer);

        // === CHAR BIGRAMS ===
        // Formato fisso: "c1 c2" (3 caratteri)
        key.resize(3);
        for (size_t i = 0; i + 1 < chars_buffer.size(); ++i) {
            key[0] = chars_buffer[i];
            key[1] = ' ';
            key[2] = chars_buffer[i + 1];
            char_bigrams[key]++;
        }

        // === CHAR TRIGRAMS ===
        // Formato fisso: "c1 c2 c3" (5 caratteri)
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

    // Merge ottimizzato (sempre piccola → grande)
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

        // Merge tree: ad ogni iterazione dimezza il numero di map attive
        while (active_maps > 1) {
            size_t pairs = active_maps / 2;

            // Merge parallelo di coppie (i, i+pairs)
            #pragma omp parallel for schedule(dynamic)
            for (size_t i = 0; i < pairs; ++i) {
                auto& target = thread_maps[i];
                auto& source = thread_maps[i + pairs];

                // Ottimizzazione: merge sempre piccola → grande
                if (source.size() > target.size()) {
                    std::swap(target, source);
                }

                // Pre-alloca spazio per ridurre rehashing
                target.reserve(target.size() + source.size());
                for (auto& [key, val] : source) {
                    target[key] += val;
                }
                source.clear();  // Libera memoria
            }

            // Gestione mappa dispari (se active_maps è dispari)
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

        // Risultato finale nella prima mappa
        result = std::move(thread_maps[0]);
    }

    // VERSIONE PARAMETRIZZATA CON SCHEDULING CONFIGURABILE
    static void process_parallel_with_schedule(
        const std::vector<std::string>& book_files,
        FrequencyCounter& word_bigrams,
        FrequencyCounter& word_trigrams,
        FrequencyCounter& char_bigrams,
        FrequencyCounter& char_trigrams,
        int num_threads,
        const std::string& schedule_type,
        int chunk_size = 1
    ) {
        omp_set_num_threads(num_threads);
        int total_books = book_files.size();

        // Mappe thread-local per evitare contention
        std::vector<std::unordered_map<std::string, size_t>> thread_wb(num_threads);
        std::vector<std::unordered_map<std::string, size_t>> thread_wt(num_threads);
        std::vector<std::unordered_map<std::string, size_t>> thread_cb(num_threads);
        std::vector<std::unordered_map<std::string, size_t>> thread_ct(num_threads);

        #pragma omp parallel
        {
            int tid = omp_get_thread_num();
            auto& my_wb = thread_wb[tid];
            auto& my_wt = thread_wt[tid];
            auto& my_cb = thread_cb[tid];
            auto& my_ct = thread_ct[tid];

            // Pre-allocazione intelligente per ridurre rehashing
            my_wb.reserve(100000);
            my_wt.reserve(200000);
            my_cb.reserve(50000);
            my_ct.reserve(100000);

            // Buffer riutilizzabili per tokenizzazione (evita allocazioni ripetute)
            std::vector<std::string> words_buf;
            std::vector<char> chars_buf;
            words_buf.reserve(10000);
            chars_buf.reserve(50000);

            // APPLICAZIONE DINAMICA DELLO SCHEDULING
            if (schedule_type == "static") {
                #pragma omp for schedule(static) nowait
                for (int i = 0; i < total_books; ++i) {
                    process_book_optimized(book_files[i], my_wb, my_wt, my_cb, my_ct, words_buf, chars_buf);
                }
            } else if (schedule_type == "dynamic") {
                #pragma omp for schedule(dynamic) nowait
                for (int i = 0; i < total_books; ++i) {
                    process_book_optimized(book_files[i], my_wb, my_wt, my_cb, my_ct, words_buf, chars_buf);
                }
            } else if (schedule_type == "guided") {
                #pragma omp for schedule(guided) nowait
                for (int i = 0; i < total_books; ++i) {
                    process_book_optimized(book_files[i], my_wb, my_wt, my_cb, my_ct, words_buf, chars_buf);
                }
            } else if (schedule_type == "static_chunk") {
                #pragma omp for schedule(static, chunk_size) nowait
                for (int i = 0; i < total_books; ++i) {
                    process_book_optimized(book_files[i], my_wb, my_wt, my_cb, my_ct, words_buf, chars_buf);
                }
            } else if (schedule_type == "dynamic_chunk") {
                #pragma omp for schedule(dynamic, chunk_size) nowait
                for (int i = 0; i < total_books; ++i) {
                    process_book_optimized(book_files[i], my_wb, my_wt, my_cb, my_ct, words_buf, chars_buf);
                }
            } else if (schedule_type == "guided_chunk") {
                #pragma omp for schedule(guided, chunk_size) nowait
                for (int i = 0; i < total_books; ++i) {
                    process_book_optimized(book_files[i], my_wb, my_wt, my_cb, my_ct, words_buf, chars_buf);
                }
            }
        }

        // Merge parallelo ottimizzato
        std::unordered_map<std::string, size_t> merged_wb, merged_wt, merged_cb, merged_ct;
        parallel_merge_aos(thread_wb, merged_wb);
        parallel_merge_aos(thread_wt, merged_wt);
        parallel_merge_aos(thread_cb, merged_cb);
        parallel_merge_aos(thread_ct, merged_ct);

        // Converti in FrequencyCounter (semplice merge)
        word_bigrams.merge(merged_wb);
        word_trigrams.merge(merged_wt);
        char_bigrams.merge(merged_cb);
        char_trigrams.merge(merged_ct);
    }

private:
    // Processa un singolo libro con buffer riutilizzabili
    static void process_book_optimized(
        const std::string& filepath,
        std::unordered_map<std::string, size_t>& wb,
        std::unordered_map<std::string, size_t>& wt,
        std::unordered_map<std::string, size_t>& cb,
        std::unordered_map<std::string, size_t>& ct,
        std::vector<std::string>& words_buf,
        std::vector<char>& chars_buf
    ) {
        // Lettura file ottimizzata: binary + ate
        std::ifstream file(filepath, std::ios::binary | std::ios::ate);
        if (!file) return;

        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        std::string text(size, '\0');
        if (!file.read(&text[0], size)) return;

        text = TextCleaner::clean_text(text);
        process_text_aos_optimized(text, wb, wt, cb, ct, words_buf, chars_buf);
    }
};

// ==================== BENCHMARK RESULT ====================
// Struttura per memorizzare i risultati di un benchmark
struct BenchmarkResult {
    std::string scheduling_type;
    int chunk_size;
    double mean;
    double min;
    double max;
    double stddev;
    double cv;  // Coefficient of variation (%)
};

// ==================== SCHEDULING BENCHMARK ====================
// Esegue test completi di tutte le strategie di scheduling
class SchedulingBenchmark {
private:
    static constexpr int WARMUP_RUNS = 1;      // Run di warm-up da scartare (ridotto)
    static constexpr int MEASURED_RUNS = 3;    // Run effettive da misurare (ridotto)
    static constexpr int NUM_RUNS = WARMUP_RUNS + MEASURED_RUNS;

    // Esegue NUM_RUNS benchmark con una specifica strategia di scheduling
    static std::vector<double> run_benchmark(
        const std::vector<std::string>& book_files,
        const std::string& schedule_type,
        int chunk_size,
        int num_threads
    ) {
        std::vector<double> run_times;
        run_times.reserve(NUM_RUNS);

        for (int run = 0; run < NUM_RUNS; ++run) {
            FrequencyCounter wb, wt, cb, ct;

            auto start_time = std::chrono::high_resolution_clock::now();

            OpenMPProcessor::process_parallel_with_schedule(
                book_files, wb, wt, cb, ct, num_threads, schedule_type, chunk_size);

            auto end_time = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> elapsed = end_time - start_time;
            run_times.push_back(elapsed.count());
        }

        return run_times;
    }

    // Analizza i risultati e calcola statistiche (scartando warm-up)
    static BenchmarkResult analyze_results(
        const std::string& schedule_type,
        int chunk_size,
        const std::vector<double>& run_times
    ) {
        // Scarta le prime WARMUP_RUNS run
        std::vector<double> measured(run_times.begin() + WARMUP_RUNS, run_times.end());

        double mean = 0.0, min_time = measured[0], max_time = measured[0];
        for (double t : measured) {
            mean += t;
            min_time = std::min(min_time, t);
            max_time = std::max(max_time, t);
        }
        mean /= measured.size();

        double stddev = 0.0;
        for (double t : measured) {
            stddev += (t - mean) * (t - mean);
        }
        stddev = std::sqrt(stddev / measured.size());

        BenchmarkResult result;
        result.scheduling_type = schedule_type;
        result.chunk_size = chunk_size;
        result.mean = mean;
        result.min = min_time;
        result.max = max_time;
        result.stddev = stddev;
        result.cv = (stddev / mean) * 100.0;  // Coefficient of variation

        return result;
    }

    // Salva risultati in CSV
    static void save_results(
        const std::vector<BenchmarkResult>& results,
        const std::string& output_dir
    ) {
        ensure_directory_exists(output_dir);

        std::ofstream csv(output_dir + "/scheduling_results.csv");
        csv << "scheduling,chunk_size,mean,min,max,stddev,cv\n";

        for (const auto& r : results) {
            csv << r.scheduling_type << ","
                << r.chunk_size << ","
                << std::fixed << std::setprecision(4)
                << r.mean << ","
                << r.min << ","
                << r.max << ","
                << r.stddev << ","
                << std::setprecision(2) << r.cv << "\n";
        }

        csv.close();
        std::cout << "\n💾 Risultati salvati in: " << output_dir
                  << "/scheduling_results.csv\n";
    }

    // Stampa tabella riassuntiva dei risultati
    static void print_summary(const std::vector<BenchmarkResult>& results) {
        std::cout << "\n╔═══════════════════════════════════════════════════════════════════════╗\n";
        std::cout << "║                         SUMMARY RESULTS                               ║\n";
        std::cout << "╠═══════════════════════════════════════════════════════════════════════╣\n";
        std::cout << "║ Strategy              │ Chunk │   Mean  │   Min   │   Max   │  CV    ║\n";
        std::cout << "╠═══════════════════════════════════════════════════════════════════════╣\n";

        for (const auto& r : results) {
            std::cout << "║ " << std::left << std::setw(20) << r.scheduling_type
                      << " │ " << std::setw(5) << (r.chunk_size == 0 ? "-" : std::to_string(r.chunk_size))
                      << " │ " << std::fixed << std::setprecision(2)
                      << std::setw(6) << r.mean << "s"
                      << " │ " << std::setw(6) << r.min << "s"
                      << " │ " << std::setw(6) << r.max << "s"
                      << " │ " << std::setprecision(1) << std::setw(5) << r.cv << "% ║\n";
        }

        std::cout << "╚═══════════════════════════════════════════════════════════════════════╝\n";

        // Trova il migliore (tempo medio minore)
        auto best = std::min_element(results.begin(), results.end(),
            [](const auto& a, const auto& b) { return a.mean < b.mean; });

        std::cout << "\n🏆 WINNER: " << best->scheduling_type;
        if (best->chunk_size > 0) {
            std::cout << " (chunk=" << best->chunk_size << ")";
        }
        std::cout << " - Mean: " << std::fixed << std::setprecision(2) << best->mean << "s"
                  << ", CV: " << best->cv << "%\n";

        // Speedup rispetto al peggiore
        auto worst = std::max_element(results.begin(), results.end(),
            [](const auto& a, const auto& b) { return a.mean < b.mean; });
        double speedup = worst->mean / best->mean;
        std::cout << "⚡ Speedup vs worst: " << std::setprecision(2) << speedup << "x\n\n";
    }

public:
    // Esegue la suite completa di test
    static void run_full_test(
        const std::vector<std::string>& book_files,
        int num_threads,
        const std::string& output_dir
    ) {
        std::vector<BenchmarkResult> results;

        std::cout << "\n╔══════════════════════════════════════════════════════╗\n";
        std::cout << "║       TEST SCHEDULING STRATEGIES (OpenMP)            ║\n";
        std::cout << "║              Lorenzo Cappetti, 2025                  ║\n";
        std::cout << "╠══════════════════════════════════════════════════════╣\n";
        std::cout << "║ Thread:       " << std::setw(36) << num_threads << " ║\n";
        std::cout << "║ Books:        " << std::setw(36) << book_files.size() << " ║\n";
        std::cout << "║ Warmup runs:  " << std::setw(36) << WARMUP_RUNS << " ║\n";
        std::cout << "║ Measured runs:" << std::setw(36) << MEASURED_RUNS << " ║\n";
        std::cout << "╚══════════════════════════════════════════════════════╝\n\n";

        // ==================== TEST 1: STATIC ====================
        std::cout << "🔄 [1/9] Testing schedule(static)...\n";
        auto times = run_benchmark(book_files, "static", 0, num_threads);
        results.push_back(analyze_results("static", 0, times));
        std::cout << "   ✓ Mean: " << std::fixed << std::setprecision(2)
                  << results.back().mean << "s\n";

        // ==================== TEST 2: DYNAMIC ====================
        std::cout << "🔄 [2/9] Testing schedule(dynamic)...\n";
        times = run_benchmark(book_files, "dynamic", 0, num_threads);
        results.push_back(analyze_results("dynamic", 0, times));
        std::cout << "   ✓ Mean: " << results.back().mean << "s\n";

        // ==================== TEST 3: GUIDED ====================
        std::cout << "🔄 [3/9] Testing schedule(guided)...\n";
        times = run_benchmark(book_files, "guided", 0, num_threads);
        results.push_back(analyze_results("guided", 0, times));
        std::cout << "   ✓ Mean: " << results.back().mean << "s\n";

        // ==================== TEST 4-6: STATIC CON CHUNK SIZE ====================
        std::vector<int> static_chunks = {1, 5, 10};
        for (size_t i = 0; i < static_chunks.size(); ++i) {
            int chunk = static_chunks[i];
            std::cout << "🔄 [" << (4+i) << "/9] Testing schedule(static, " << chunk << ")...\n";
            times = run_benchmark(book_files, "static_chunk", chunk, num_threads);
            results.push_back(analyze_results("static_" + std::to_string(chunk), chunk, times));
            std::cout << "   ✓ Mean: " << results.back().mean << "s\n";
        }

        // ==================== TEST 7-8: DYNAMIC CON CHUNK SIZE ====================
        std::vector<int> dynamic_chunks = {1, 5};
        for (size_t i = 0; i < dynamic_chunks.size(); ++i) {
            int chunk = dynamic_chunks[i];
            std::cout << "🔄 [" << (7+i) << "/9] Testing schedule(dynamic, " << chunk << ")...\n";
            times = run_benchmark(book_files, "dynamic_chunk", chunk, num_threads);
            results.push_back(analyze_results("dynamic_" + std::to_string(chunk), chunk, times));
            std::cout << "   ✓ Mean: " << results.back().mean << "s\n";
        }

        // ==================== TEST 9: GUIDED CON CHUNK SIZE ====================
        std::cout << "🔄 [9/9] Testing schedule(guided, 5)...\n";
        times = run_benchmark(book_files, "guided_chunk", 5, num_threads);
        results.push_back(analyze_results("guided_5", 5, times));
        std::cout << "   ✓ Mean: " << results.back().mean << "s\n";

        // ==================== STAMPA RISULTATI (NO SALVATAGGIO) ====================
        print_summary(results);
    }
};

// ==================== MAIN ====================
int main() {
    std::cout << "\n╔═══════════════════════════════════════════════════════╗\n";
    std::cout << "║     OpenMP Scheduling Strategies Benchmark            ║\n";
    std::cout << "║             Lorenzo Cappetti, 2025                    ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════╝\n\n";

    // Configurazione fissa: 8 thread
    const int NUM_THREADS = 8;
    std::cout << "🧵 Thread configurati: " << NUM_THREADS << " (fisso)\n";

    // Carica lista libri
    std::string folder_path = "/Users/lorenzocappetti/CLionProjects/Bigrams_Trigrams/book_gutenberg";

    if (!fs::exists(folder_path) || !fs::is_directory(folder_path)) {
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

    std::cout << "📚 Trovati " << book_files.size() << " libri\n";

    // Directory output
    std::string output_dir = "test/scheduling_tests";

    // Esegui la suite completa di test
    SchedulingBenchmark::run_full_test(book_files, NUM_THREADS, output_dir);

    std::cout << "\n✅ Test completato!\n";
    std::cout << "📊 Analizza i risultati con:\n";
    std::cout << "   cat test/scheduling_tests/scheduling_results.csv\n\n";

    return 0;
}
