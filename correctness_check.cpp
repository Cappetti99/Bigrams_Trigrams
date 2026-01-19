//
// Lorenzo Cappetti, 2025 - Correctness Checker
// Confronta i risultati tra versione sequenziale e parallela
//
// Verify that parallel optimizations don't break correctness:
// compares seq vs parallel CSV outputs, checking both n-gram presence and frequencies

#include <iostream>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <iomanip>
#include <cmath>
#include <filesystem>

namespace fs = std::filesystem;

// ==================== CSV READER ====================
// Parse CSV files into hash maps for comparison
// Format: "ngram",frequency (e.g., "hello world",42)
class CSVReader {
public:
    static std::unordered_map<std::string, size_t> read_csv(const std::string& filename) {
        std::unordered_map<std::string, size_t> result;
        std::ifstream file(filename);

        if (!file) {
            std::cerr << "❌ File non trovato: " << filename << "\n";
            return result;
        }

        std::string line;
        // Skip CSV header
        std::getline(file, line);

        while (std::getline(file, line)) {
            if (line.empty()) continue;

            // Parse CSV manually: find quotes around ngram, then comma before frequency
            // Example: "hello world",42 -> ngram="hello world", freq=42
            size_t first_quote = line.find('"');
            size_t second_quote = line.find('"', first_quote + 1);
            size_t comma = line.find(',', second_quote);

            if (first_quote != std::string::npos &&
                second_quote != std::string::npos &&
                comma != std::string::npos) {

                std::string ngram = line.substr(first_quote + 1, second_quote - first_quote - 1);
                std::string freq_str = line.substr(comma + 1);

                try {
                    size_t frequency = std::stoull(freq_str);
                    result[ngram] = frequency;
                } catch (...) {
                    std::cerr << "⚠️  Errore parsing linea: " << line << "\n";
                }
            }
        }

        return result;
    }
};

// ==================== CORRECTNESS ANALYZER ====================
// Core verification: parallel should match sequential exactly (same ngrams, same frequencies)
class CorrectnessAnalyzer {
private:
    struct ComparisonResult {
        std::string name;           // "Word Bigrams", etc.
        size_t seq_unique;          // Distinct ngrams in sequential
        size_t par_unique;          // Distinct ngrams in parallel
        size_t seq_total;           // Sum of all frequencies (seq)
        size_t par_total;           // Sum of all frequencies (par)
        size_t matches;             // Perfect matches (same ngram, same freq)
        size_t freq_mismatches;     // Same ngram, different frequency -> BUG
        size_t only_in_seq;         // Missing in parallel -> BUG
        size_t only_in_par;         // Spurious in parallel -> BUG
        bool is_correct;            // True if zero mismatches/missing/spurious
        double accuracy_percent;    // matches / max(seq,par) * 100
    };

    // Compare two hash maps (seq vs par) and categorize differences
    // Returns metrics: matches, frequency mismatches, missing/spurious entries
    static ComparisonResult compare_maps(
        const std::unordered_map<std::string, size_t>& seq_map,
        const std::unordered_map<std::string, size_t>& par_map,
        const std::string& name,
        bool verbose = false
    ) {
        ComparisonResult result;
        result.name = name;
        result.seq_unique = seq_map.size();
        result.par_unique = par_map.size();

        // Total frequency sums (should also match if correct)
        result.seq_total = 0;
        for (const auto& [_, count] : seq_map) result.seq_total += count;

        result.par_total = 0;
        for (const auto& [_, count] : par_map) result.par_total += count;

        result.matches = 0;
        result.freq_mismatches = 0;
        result.only_in_seq = 0;
        result.only_in_par = 0;

        std::vector<std::string> mismatch_examples;
        std::vector<std::string> seq_only_examples;
        std::vector<std::string> par_only_examples;

        // Check each sequential ngram: is it in parallel? Does frequency match?
        for (const auto& [key, val_seq] : seq_map) {
            auto it = par_map.find(key);
            if (it == par_map.end()) {
                result.only_in_seq++;  // Missing in parallel -> error
                if (verbose && seq_only_examples.size() < 5) {
                    seq_only_examples.push_back("\"" + key + "\" (freq=" + std::to_string(val_seq) + ")");
                }
            } else if (it->second != val_seq) {
                result.freq_mismatches++;  // Found but wrong frequency -> race condition?
                if (verbose && mismatch_examples.size() < 5) {
                    mismatch_examples.push_back(
                        "\"" + key + "\" seq=" + std::to_string(val_seq) +
                        " vs par=" + std::to_string(it->second)
                    );
                }
            } else {
                result.matches++;
            }
        }

        // Reverse check: any ngrams in parallel that aren't in sequential?
        for (const auto& [key, val_par] : par_map) {
            if (seq_map.find(key) == seq_map.end()) {
                result.only_in_par++;  // Spurious entry -> error
                if (verbose && par_only_examples.size() < 5) {
                    par_only_examples.push_back("\"" + key + "\" (freq=" + std::to_string(val_par) + ")");
                }
            }
        }

        // Pass only if zero errors (strict correctness check)
        result.is_correct = (result.freq_mismatches == 0 &&
                            result.only_in_seq == 0 &&
                            result.only_in_par == 0);

        // Accuracy: what % of ngrams match perfectly?
        size_t total_ngrams = std::max(result.seq_unique, result.par_unique);
        if (total_ngrams > 0) {
            result.accuracy_percent = (double)result.matches / total_ngrams * 100.0;
        } else {
            result.accuracy_percent = 100.0;
        }

        // If verbose mode and errors found, show examples for debugging
        if (verbose && !result.is_correct) {
            std::cout << "\n  📊 Dettagli per " << name << ":\n";

            if (!mismatch_examples.empty()) {
                std::cout << "  ⚠️  Esempi di frequency mismatch:\n";
                for (const auto& ex : mismatch_examples) {
                    std::cout << "     - " << ex << "\n";
                }
            }

            if (!seq_only_examples.empty()) {
                std::cout << "  ⚠️  Esempi presenti solo in seq:\n";
                for (const auto& ex : seq_only_examples) {
                    std::cout << "     - " << ex << "\n";
                }
            }

            if (!par_only_examples.empty()) {
                std::cout << "  ⚠️  Esempi presenti solo in par:\n";
                for (const auto& ex : par_only_examples) {
                    std::cout << "     - " << ex << "\n";
                }
            }
        }

        return result;
    }

public:    // Main verification: load both output directories, compare all 4 n-gram types
    // Fails if ANY difference found (strict equality required)    static void check_correctness(
        const std::string& seq_dir = "test/output_sequential",
        const std::string& par_dir = "test/output_hybrid",
        bool verbose = true
    ) {
        std::cout << "\n";
        std::cout << "\n";
        std::cout << "CORRECTNESS CHECK ANALYZER\n";
        std::cout << "Confronto SEQ vs PARALLEL\n";
        std::cout << "=========================\n\n";

        // Verifica che entrambe le directory esistano
        if (!fs::exists(seq_dir)) {
            std::cerr << "❌ Directory '" << seq_dir << "' non trovata!\n";
            return;
        }
        if (!fs::exists(par_dir)) {
            std::cerr << "❌ Directory '" << par_dir << "' non trovata!\n";
            return;
        }

        std::cout << "Sequential output: " << seq_dir << "/\n";
        std::cout << "Parallel output:   " << par_dir << "/\n\n";

        // Load all 4 n-gram types from both directories (8 files total)
        std::cout << "Caricamento files CSV...\n\n";

        auto seq_wb = CSVReader::read_csv(seq_dir + "/word_bigrams.csv");
        auto par_wb = CSVReader::read_csv(par_dir + "/word_bigrams.csv");

        auto seq_wt = CSVReader::read_csv(seq_dir + "/word_trigrams.csv");
        auto par_wt = CSVReader::read_csv(par_dir + "/word_trigrams.csv");

        auto seq_cb = CSVReader::read_csv(seq_dir + "/char_bigrams.csv");
        auto par_cb = CSVReader::read_csv(par_dir + "/char_bigrams.csv");

        auto seq_ct = CSVReader::read_csv(seq_dir + "/char_trigrams.csv");
        auto par_ct = CSVReader::read_csv(par_dir + "/char_trigrams.csv");

        // Run 4 comparisons (verbose=true shows error examples if found)
        std::vector<ComparisonResult> results;

        std::cout << "Analisi in corso...\n\n";

        results.push_back(compare_maps(seq_wb, par_wb, "Word Bigrams", verbose));
        results.push_back(compare_maps(seq_wt, par_wt, "Word Trigrams", verbose));
        results.push_back(compare_maps(seq_cb, par_cb, "Char Bigrams", verbose));
        results.push_back(compare_maps(seq_ct, par_ct, "Char Trigrams", verbose));

        // Print formatted comparison table for each n-gram type
        std::cout << "                  CONFRONTO DEI RISULTATI                  \n";

        bool all_correct = true;

        for (const auto& res : results) {
            std::cout << "-----------------------------------------------------\n";
            std::cout << "| " << std::left << std::setw(51) << res.name << " |\n";
            std::cout << "-----------------------------------------------------\n";

            // Statistiche base
            std::cout << "| Unique (seq/par): " << std::right << std::setw(10) << res.seq_unique << " / " << std::setw(10) << res.par_unique << " |\n";
            std::cout << "| Total  (seq/par): " << std::right << std::setw(10) << res.seq_total << " / " << std::setw(10) << res.par_total << " |\n";
            std::cout << "-----------------------------------------------------\n";

            // Confronto
            std::cout << "| Matches:          " << std::right << std::setw(27) << res.matches << " |\n";
            std::cout << "| Mismatches:       " << std::right << std::setw(27) << res.freq_mismatches << " |\n";
            std::cout << "| Only in seq/par:  " << std::right << std::setw(10) << res.only_in_seq << " / " << std::setw(10) << res.only_in_par << " |\n";
            std::cout << "-----------------------------------------------------\n";

            // Accuracy
            std::cout << "| Accuracy:             " << std::right << std::setw(23) << std::fixed
                      << std::setprecision(4) << res.accuracy_percent << " %  |\n";

            // Status
            std::cout << "| Status:               ";
            if (res.is_correct) {
                std::cout << "\033[32m" << std::setw(27) << "OK" << "\033[0m |\n";
            } else {
                std::cout << "\033[31m" << std::setw(27) << "ERROR" << "\033[0m |\n";
                all_correct = false;
            }

            std::cout << "-----------------------------------------------------\n\n";
        }

        // Final verdict: parallel must match sequential 100% to pass
        if (all_correct) {
            std::cout << "[SUCCESS] CORRECTNESS CHECK PASSED\n";
            std::cout << "La versione parallela produce risultati IDENTICI alla versione sequenziale!\n";
        } else {
            std::cout << "[FAILED] CORRECTNESS CHECK FAILED\n";
            std::cout << "Trovate DIFFERENZE tra seq e parallel!\n";
        }
        std::cout << "\n";

        // Overall stats across all 4 n-gram types
        size_t total_matches = 0;
        size_t total_errors = 0;

        for (const auto& res : results) {
            total_matches += res.matches;
            total_errors += res.freq_mismatches + res.only_in_seq + res.only_in_par;
        }

        std::cout << " Statistiche complessive:\n";
        std::cout << "   • Ngrams totali verificati: " << (total_matches + total_errors) << "\n";
        std::cout << "   • Matches perfetti:         " << total_matches << "\n";
        std::cout << "   • Errori totali:            " << total_errors << "\n";

        if (total_matches + total_errors > 0) {
            double overall_accuracy = (double)total_matches / (total_matches + total_errors) * 100.0;
            std::cout << "   • Accuracy complessiva:     " << std::fixed << std::setprecision(4)
                      << overall_accuracy << "%\n";
        }

        std::cout << "\n";
    }
};

// ==================== MAIN ====================
int main(int argc, char* argv[]) {
    std::string seq_dir = "test/output_sequential";
    std::string par_dir = "test/output_hybrid";
    bool verbose = true;

    // Opzionale: parsing argomenti da linea di comando
    if (argc >= 3) {
        seq_dir = argv[1];
        par_dir = argv[2];
    }
    if (argc >= 4) {
        verbose = (std::string(argv[3]) == "verbose" || std::string(argv[3]) == "v");
    }

    CorrectnessAnalyzer::check_correctness(seq_dir, par_dir, verbose);

    return 0;
}
