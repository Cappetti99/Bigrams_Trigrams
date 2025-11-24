//
// Test di correttezza COMPLETO - Lorenzo Cappetti, 2025
// Verifica che tutti e 3 i programmi producano gli stessi risultati
// Compila ed esegue automaticamente i 3 programmi su test_file.txt e confronta i risultati
//

#include <iostream>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <cstdlib>
#include <array>
#include <memory>
#include <stdexcept>

//═══════════════════════════════════════════════════════════════
// UTILITY FUNCTIONS
//═══════════════════════════════════════════════════════════════

// Esegue un comando shell e cattura l'output
std::string exec_command(const char* cmd) {
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
    if (!pipe) {
        throw std::runtime_error("popen() failed!");
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

// Estrae un valore numerico da una stringa (es: "Total unique: 34" -> 34)
size_t extract_number(const std::string& line) {
    std::istringstream iss(line);
    std::string word;
    while (iss >> word) {
        // Rimuovi caratteri non numerici
        std::string clean_word;
        for (char c : word) {
            if (std::isdigit(c)) clean_word += c;
        }
        if (!clean_word.empty()) {
            return std::stoull(clean_word);
        }
    }
    return 0;
}

// Struttura per le statistiche
struct Stats {
    size_t word_bigrams_unique = 0;
    size_t word_trigrams_unique = 0;
    size_t char_bigrams_unique = 0;
    size_t char_trigrams_unique = 0;
    std::string top_word_bigram;
    size_t top_word_bigram_freq = 0;
    std::string top_char_trigram;
    size_t top_char_trigram_freq = 0;
};

Stats parse_output(const std::string& output) {
    Stats stats;
    std::istringstream iss(output);
    std::string line;

    bool in_word_bigrams = false;
    bool in_word_trigrams = false;
    bool in_char_bigrams = false;
    bool in_char_trigrams = false;
    bool got_first_wb = false;
    bool got_first_ct = false;

    while (std::getline(iss, line)) {
        // Rileva sezioni
        if (line.find("Word Bigrams") != std::string::npos ||
            line.find("WORD BIGRAMS") != std::string::npos) {
            in_word_bigrams = true;
            in_word_trigrams = in_char_bigrams = in_char_trigrams = false;
        } else if (line.find("Word Trigrams") != std::string::npos ||
                   line.find("WORD TRIGRAMS") != std::string::npos) {
            in_word_trigrams = true;
            in_word_bigrams = in_char_bigrams = in_char_trigrams = false;
        } else if (line.find("Char Bigrams") != std::string::npos ||
                   line.find("CHAR BIGRAMS") != std::string::npos) {
            in_char_bigrams = true;
            in_word_bigrams = in_word_trigrams = in_char_trigrams = false;
        } else if (line.find("Char Trigrams") != std::string::npos ||
                   line.find("CHAR TRIGRAMS") != std::string::npos) {
            in_char_trigrams = true;
            in_word_bigrams = in_word_trigrams = in_char_bigrams = false;
        }

        // Estrai unique counts
        if (line.find("Total unique") != std::string::npos ||
            line.find("unique)") != std::string::npos ||
            line.find("Unique Word Bigrams") != std::string::npos ||
            line.find("Unique Word Trigrams") != std::string::npos ||
            line.find("Unique Char Bigrams") != std::string::npos ||
            line.find("Unique Char Trigrams") != std::string::npos ||
            line.find("n-grams saved to") != std::string::npos) { // Added support for new format

            size_t num = extract_number(line);

            // Prova a identificare il tipo in base al contesto della linea
            if ((line.find("Word Bigram") != std::string::npos || line.find("Word Bigrams") != std::string::npos) && stats.word_bigrams_unique == 0) {
                stats.word_bigrams_unique = num;
            } else if ((line.find("Word Trigram") != std::string::npos || line.find("Word Trigrams") != std::string::npos) && stats.word_trigrams_unique == 0) {
                stats.word_trigrams_unique = num;
            } else if ((line.find("Char Bigram") != std::string::npos || line.find("Char Bigrams") != std::string::npos) && stats.char_bigrams_unique == 0) {
                stats.char_bigrams_unique = num;
            } else if ((line.find("Char Trigram") != std::string::npos || line.find("Char Trigrams") != std::string::npos) && stats.char_trigrams_unique == 0) {
                stats.char_trigrams_unique = num;
            } else if (num > 0) {
                // Fallback: usa lo stato della sezione
                if (in_word_bigrams && stats.word_bigrams_unique == 0) {
                    stats.word_bigrams_unique = num;
                } else if (in_word_trigrams && stats.word_trigrams_unique == 0) {
                    stats.word_trigrams_unique = num;
                } else if (in_char_bigrams && stats.char_bigrams_unique == 0) {
                    stats.char_bigrams_unique = num;
                } else if (in_char_trigrams && stats.char_trigrams_unique == 0) {
                    stats.char_trigrams_unique = num;
                }
            }
        }

        // Estrai top n-grams (primo della lista)
        if (line.find(". \"") != std::string::npos) {
            size_t first_quote = line.find("\"");
            size_t second_quote = line.find("\"", first_quote + 1);

            if (first_quote != std::string::npos && second_quote != std::string::npos) {
                std::string ngram = line.substr(first_quote + 1, second_quote - first_quote - 1);

                // Trova la frequenza
                size_t freq = 0;
                size_t occ_pos = line.find("occurrences");
                if (occ_pos != std::string::npos) {
                    std::istringstream iss_line(line.substr(second_quote + 1, occ_pos - second_quote - 1));
                    std::string word;
                    while (iss_line >> word) {
                        std::string clean_word;
                        for (char c : word) {
                            if (std::isdigit(c)) clean_word += c;
                        }
                        if (!clean_word.empty()) {
                            freq = std::stoull(clean_word);
                        }
                    }
                } else {
                    size_t arrow_pos = line.find("->", second_quote);
                    if (arrow_pos != std::string::npos) {
                        std::istringstream iss_line(line.substr(arrow_pos + 2));
                        std::string word;
                        while (iss_line >> word) {
                            std::string clean_word;
                            for (char c : word) {
                                if (std::isdigit(c)) clean_word += c;
                            }
                            if (!clean_word.empty()) {
                                freq = std::stoull(clean_word);
                                break;
                            }
                        }
                    }
                }

                if (in_word_bigrams && !got_first_wb && freq > 0) {
                    stats.top_word_bigram = ngram;
                    stats.top_word_bigram_freq = freq;
                    got_first_wb = true;
                } else if (in_char_trigrams && !got_first_ct && freq > 0) {
                    stats.top_char_trigram = ngram;
                    stats.top_char_trigram_freq = freq;
                    got_first_ct = true;
                }
            }
        }
    }

    return stats;
}

//═══════════════════════════════════════════════════════════════
// MAIN
//═══════════════════════════════════════════════════════════════
int main() {
    std::cout << "N-GRAM CORRECTNESS TEST\n\n";

    std::string base_path = "/Users/lorenzocappetti/CLionProjects/Bigrams_Trigrams";
    std::string test_file_source = base_path + "/test_data/test_file.txt";
    std::string test_file_dest = base_path + "/book_gutenberg/test_file.txt";
    std::string temp_dir = base_path + "/book_gutenberg/temp_backup";

    std::cout << "PHASE 1: Environment Setup\n";

    // Backup temporaneo di tutti i libri
    std::cout << "   Backing up books...\n";
    std::string mkdir_cmd = "mkdir -p " + temp_dir;
    system(mkdir_cmd.c_str());
    std::string mv_cmd = "cd " + base_path + "/book_gutenberg && mv libro_*.txt temp_backup/ 2>/dev/null || true";
    system(mv_cmd.c_str());

    // Copia il test file da test_data a book_gutenberg
    std::cout << "   Copying test_file.txt...\n";
    std::string cp_cmd = "cp " + test_file_source + " " + test_file_dest;
    system(cp_cmd.c_str());

    std::cout << "   Test file ready.\n\n";

    // Verifica che test_file.txt sia stato copiato
    std::ifstream test_check(test_file_dest);
    if (!test_check) {
        std::cerr << "   Error: test_file.txt not copied!\n";
        return 1;
    }
    test_check.close();

    std::cout << "PHASE 2: Compilation\n";

    struct Program {
        std::string name;
        std::string source;
        std::string binary;
        std::string compile_cmd;
        bool needs_openmp;
    };

    // Prima verifica quali file .cpp esistono nella directory
    std::cout << "   Searching source files...\n";

    std::vector<std::string> available_sources;
    std::string ls_cmd = "cd " + base_path + " && ls *.cpp 2>/dev/null";
    std::string ls_output = exec_command(ls_cmd.c_str());

    std::istringstream iss(ls_output);
    std::string filename;
    while (iss >> filename) {
        available_sources.push_back(filename);
        std::cout << "      Found: " << filename << "\n";
    }

    std::vector<Program> programs;

    // seq.cpp (sempre presente)
    if (std::find(available_sources.begin(), available_sources.end(), "seq.cpp") != available_sources.end()) {
        programs.push_back({"seq", "seq.cpp", "seq_test",
                           "clang++ -std=c++17 -O2 seq.cpp -o seq_test 2>&1", false});
    }

    // parallel.cpp (se esiste)
    if (std::find(available_sources.begin(), available_sources.end(), "parallel.cpp") != available_sources.end()) {
        programs.push_back({"parallel", "parallel.cpp", "parallel_test",
                           "clang++ -std=c++17 -O2 -Xpreprocessor -fopenmp parallel.cpp -lomp -o parallel_test -I/opt/homebrew/opt/libomp/include -L/opt/homebrew/opt/libomp/lib 2>&1", true});
    }

    // Cerca varianti del file hybrid
    std::vector<std::string> hybrid_variants = {
        "parallel_AoS_SoA.cpp",
        "hybrid.cpp",
        "hybrid_AoS_SoA.cpp"
    };

    for (const auto& variant : hybrid_variants) {
        if (std::find(available_sources.begin(), available_sources.end(), variant) != available_sources.end()) {
            std::string binary_name = variant.substr(0, variant.find(".cpp")) + "_test";
            programs.push_back({"hybrid", variant, binary_name,
                               "clang++ -std=c++17 -O2 -Xpreprocessor -fopenmp " + variant + " -lomp -o " + binary_name + " -I/opt/homebrew/opt/libomp/include -L/opt/homebrew/opt/libomp/lib 2>&1", true});
            break; // Prendi solo la prima variante trovata
        }
    }

    if (programs.empty()) {
        std::cerr << "   No source files found!\n";
        return 1;
    }

    std::cout << "   Found " << programs.size() << " programs to test\n\n";


    for (auto& prog : programs) {
        std::cout << "   Compiling " << prog.name << "...\n";
        std::string cmd = "cd " + base_path + " && " + prog.compile_cmd;
        std::string result = exec_command(cmd.c_str());

        if (result.find("error") != std::string::npos) {
            std::cerr << "   Compilation error " << prog.name << ":\n";
            std::cerr << result << "\n";

            // Ripristina i file
            std::string restore = "cd " + base_path + "/book_gutenberg && mv temp_backup/* . 2>/dev/null || true && rmdir temp_backup 2>/dev/null || true";
            system(restore.c_str());
            return 1;
        }
        std::cout << "   " << prog.name << " compiled\n";
    }
    std::cout << "\n";

    std::cout << "PHASE 3: Execution\n";

    std::vector<Stats> all_stats;

    for (auto& prog : programs) {
        std::cout << "   Running " << prog.name << "...\n";

        std::string run_cmd;
        if (prog.needs_openmp) {
            // Fornisci input automatico: 4 thread
            run_cmd = "cd " + base_path + " && echo '4' | ./" + prog.binary + " 2>&1";
        } else {
            run_cmd = "cd " + base_path + " && ./" + prog.binary + " 2>&1";
        }

        std::string output = exec_command(run_cmd.c_str());

        // Parse output
        Stats stats = parse_output(output);
        all_stats.push_back(stats);

        std::cout << "      Word Bigrams:  " << stats.word_bigrams_unique << " unique\n";
        std::cout << "      Word Trigrams: " << stats.word_trigrams_unique << " unique\n";
        std::cout << "      Char Bigrams:  " << stats.char_bigrams_unique << " unique\n";
        std::cout << "      Char Trigrams: " << stats.char_trigrams_unique << " unique\n";
        if (!stats.top_word_bigram.empty()) {
            std::cout << "      Top Word Bigram: \"" << stats.top_word_bigram
                      << "\" (" << stats.top_word_bigram_freq << ")\n";
        }
        if (!stats.top_char_trigram.empty()) {
            std::cout << "      Top Char Trigram: \"" << stats.top_char_trigram
                      << "\" (" << stats.top_char_trigram_freq << ")\n";
        }
        std::cout << "\n";
    }

    std::cout << "PHASE 4: Comparison\n";

    bool all_match = true;

    // Confronta con i valori attesi
    const size_t EXPECTED_WB = 34;
    const size_t EXPECTED_WT = 37;
    const size_t EXPECTED_CB = 91;
    const size_t EXPECTED_CT = 124;
    const std::string EXPECTED_TOP_WB = "hello world";
    const size_t EXPECTED_TOP_WB_FREQ = 3;
    const std::string EXPECTED_TOP_CT = "the";
    const size_t EXPECTED_TOP_CT_FREQ = 8;

    for (size_t i = 0; i < programs.size(); ++i) {
        std::cout << "\n   Verifying " << programs[i].name << ":\n";

        bool prog_ok = true;

        if (all_stats[i].word_bigrams_unique != EXPECTED_WB) {
            std::cout << "      Word Bigrams: FAIL (expected " << EXPECTED_WB
                      << ", got " << all_stats[i].word_bigrams_unique << ")\n";
            prog_ok = false;
        } else {
            std::cout << "      Word Bigrams: OK\n";
        }

        if (all_stats[i].word_trigrams_unique != EXPECTED_WT) {
            std::cout << "      Word Trigrams: FAIL (expected " << EXPECTED_WT
                      << ", got " << all_stats[i].word_trigrams_unique << ")\n";
            prog_ok = false;
        } else {
            std::cout << "      Word Trigrams: OK\n";
        }

        if (all_stats[i].char_bigrams_unique != EXPECTED_CB) {
            std::cout << "      Char Bigrams: FAIL (expected " << EXPECTED_CB
                      << ", got " << all_stats[i].char_bigrams_unique << ")\n";
            prog_ok = false;
        } else {
            std::cout << "      Char Bigrams: OK\n";
        }

        if (all_stats[i].char_trigrams_unique != EXPECTED_CT) {
            std::cout << "      Char Trigrams: FAIL (expected " << EXPECTED_CT
                      << ", got " << all_stats[i].char_trigrams_unique << ")\n";
            prog_ok = false;
        } else {
            std::cout << "      Char Trigrams: OK\n";
        }

        // Verifica top n-grams (informativo)
        // Verifica top n-grams (informativo - opzionale)
        if (!all_stats[i].top_word_bigram.empty()) {
             if (all_stats[i].top_word_bigram == EXPECTED_TOP_WB &&
                all_stats[i].top_word_bigram_freq == EXPECTED_TOP_WB_FREQ) {
                std::cout << "      Top Word Bigram: OK (\"" << EXPECTED_TOP_WB << "\" "
                          << EXPECTED_TOP_WB_FREQ << ")\n";
            } else {
                std::cout << "      Top Word Bigram: \"" << all_stats[i].top_word_bigram
                          << "\" (" << all_stats[i].top_word_bigram_freq << ")\n";
            }
        }

        if (!all_stats[i].top_char_trigram.empty()) {
            if (all_stats[i].top_char_trigram == EXPECTED_TOP_CT &&
                all_stats[i].top_char_trigram_freq == EXPECTED_TOP_CT_FREQ) {
                std::cout << "      Top Char Trigram: OK (\"" << EXPECTED_TOP_CT << "\" "
                          << EXPECTED_TOP_CT_FREQ << ")\n";
            } else {
                std::cout << "      Top Char Trigram: \"" << all_stats[i].top_char_trigram
                          << "\" (" << all_stats[i].top_char_trigram_freq << ")\n";
            }
        }

        if (!prog_ok) {
            all_match = false;
        }
    }

    // Confronto tra programmi
    std::cout << "\n   Comparing consistency:\n";
    bool programs_agree = true;

    for (size_t i = 1; i < all_stats.size(); ++i) {
        if (all_stats[i].word_bigrams_unique != all_stats[0].word_bigrams_unique ||
            all_stats[i].word_trigrams_unique != all_stats[0].word_trigrams_unique ||
            all_stats[i].char_bigrams_unique != all_stats[0].char_bigrams_unique ||
            all_stats[i].char_trigrams_unique != all_stats[0].char_trigrams_unique) {

            std::cout << "      " << programs[i].name << " differs from " << programs[0].name << "\n";
            programs_agree = false;
        }
    }

    if (programs_agree) {
        std::cout << "      All programs match\n";
    }

    std::cout << "\n";
    std::cout << "PHASE 5: Cleanup\n";

    // Rimuovi test file da book_gutenberg
    std::cout << "   Removing temp files...\n";
    std::string rm_test = "rm -f " + test_file_dest;
    system(rm_test.c_str());

    // Ripristina i libri
    std::cout << "   Restoring books...\n";
    std::string restore = "cd " + base_path + "/book_gutenberg && mv temp_backup/* . 2>/dev/null || true && rmdir temp_backup 2>/dev/null || true";
    system(restore.c_str());

    // Rimuovi binari di test
    for (const auto& prog : programs) {
        std::string rm_cmd = "rm -f " + base_path + "/" + prog.binary;
        system(rm_cmd.c_str());
    }

    std::cout << "   Environment restored\n\n";

    // Risultato finale
    if (all_match && programs_agree) {
        std::cout << "[SUCCESS] All programs correct and consistent.\n";
    } else if (programs_agree && !all_match) {
        std::cout << "[PARTIAL] Programs consistent but results differ from expected.\n";
    } else {
        std::cout << "[FAILED] Inconsistent results.\n";
    }
    std::cout << "\n";

    return (all_match && programs_agree) ? 0 : 1;
}
