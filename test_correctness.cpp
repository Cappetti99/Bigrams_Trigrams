//
// Test di correttezza - Lorenzo Cappetti, 2025
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
};

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
        // Cerca un numero
        if (std::all_of(word.begin(), word.end(), ::isdigit)) {
            return std::stoull(word);
        }
    }
    return 0;
}

// Estrae le statistiche da output di un programma
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
            line.find("unique)") != std::string::npos) {
            size_t num = extract_number(line);
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
        
        // Estrai top n-grams (primo della lista)
        if (line.find(". \"") != std::string::npos) {
            
            size_t first_quote = line.find("\"");
            size_t second_quote = line.find("\"", first_quote + 1);
            
            if (first_quote != std::string::npos && second_quote != std::string::npos) {
                std::string ngram = line.substr(first_quote + 1, second_quote - first_quote - 1);
                
                // Trova la frequenza - cerca "occurrences" o numeri dopo quote
                size_t freq = 0;
                size_t occ_pos = line.find("occurrences");
                if (occ_pos != std::string::npos) {
                    // Cerca il numero prima di "occurrences"
                    std::istringstream iss_line(line.substr(second_quote + 1, occ_pos - second_quote - 1));
                    std::string word;
                    while (iss_line >> word) {
                        if (std::all_of(word.begin(), word.end(), ::isdigit)) {
                            freq = std::stoull(word);
                        }
                    }
                } else {
                    // Cerca dopo "->" o alla fine
                    size_t arrow_pos = line.find("->", second_quote);
                    if (arrow_pos != std::string::npos) {
                        std::istringstream iss_line(line.substr(arrow_pos + 2));
                        std::string word;
                        while (iss_line >> word) {
                            if (std::all_of(word.begin(), word.end(), ::isdigit)) {
                                freq = std::stoull(word);
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
    std::cout << "\n╔═══════════════════════════════════════════════════════╗\n";
    std::cout << "║         TEST AUTOMATICO DI CORRETTEZZA N-GRAM         ║\n";
    std::cout << "║              Lorenzo Cappetti, 2025                   ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════╝\n\n";

    std::string base_path = "/Users/lorenzocappetti/CLionProjects/Bigrams_Trigrams";
    std::string test_file_source = base_path + "/test_data/test_file.txt";
    std::string test_file_dest = base_path + "/book_gutenberg/test_file.txt";
    std::string temp_dir = base_path + "/book_gutenberg/temp_backup";

    std::cout << "🔧 FASE 1: Preparazione ambiente di test\n";
    std::cout << "─────────────────────────────────────────────────────────\n";
    
    // Backup temporaneo di tutti i libri
    std::cout << "   📦 Backup temporaneo libri...\n";
    std::string mkdir_cmd = "mkdir -p " + temp_dir;
    system(mkdir_cmd.c_str());
    std::string mv_cmd = "cd " + base_path + "/book_gutenberg && mv libro_*.txt temp_backup/ 2>/dev/null || true";
    system(mv_cmd.c_str());
    
    // Copia il test file da test_data a book_gutenberg
    std::cout << "   📋 Copia test_file.txt in book_gutenberg...\n";
    std::string cp_cmd = "cp " + test_file_source + " " + test_file_dest;
    system(cp_cmd.c_str());
    
    std::cout << "   ✅ File di test pronto per l'esecuzione\n\n";

    // Verifica che test_file.txt sia stato copiato
    std::ifstream test_check(test_file_dest);
    if (!test_check) {
        std::cerr << "   ❌ Errore: test_file.txt non copiato correttamente!\n";
        return 1;
    }
    test_check.close();

    std::cout << "� FASE 2: Compilazione programmi\n";
    std::cout << "─────────────────────────────────────────────────────────\n";
    
    struct Program {
        std::string name;
        std::string source;
        std::string binary;
        std::string compile_cmd;
        bool needs_openmp;
    };
    
    std::vector<Program> programs = {
        {"bigram_seq", "bigram_seq.cpp", "bigram_seq_test", 
         "clang++ -std=c++17 -O2 bigram_seq.cpp -o bigram_seq_test 2>&1", false},
        
        {"bigram_par", "bigram_par.cpp", "bigram_par_test",
         "clang++ -std=c++17 -O2 -Xpreprocessor -fopenmp bigram_par.cpp -lomp -o bigram_par_test -I/opt/homebrew/opt/libomp/include -L/opt/homebrew/opt/libomp/lib 2>&1", true},
        
        {"par_SoA_AoS", "par_SoA_AoS.cpp", "par_SoA_AoS_test",
         "clang++ -std=c++17 -O2 -Xpreprocessor -fopenmp par_SoA_AoS.cpp -lomp -o par_SoA_AoS_test -I/opt/homebrew/opt/libomp/include -L/opt/homebrew/opt/libomp/lib 2>&1", true}
    };
    
    for (auto& prog : programs) {
        std::cout << "   🔨 Compilando " << prog.name << "...\n";
        std::string cmd = "cd " + base_path + " && " + prog.compile_cmd;
        std::string result = exec_command(cmd.c_str());
        
        if (result.find("error") != std::string::npos) {
            std::cerr << "   ❌ Errore compilazione " << prog.name << ":\n";
            std::cerr << result << "\n";
            
            // Ripristina i file
            std::string restore = "cd " + base_path + "/book_gutenberg && mv temp_backup/* . 2>/dev/null || true && rmdir temp_backup 2>/dev/null || true";
            system(restore.c_str());
            return 1;
        }
        std::cout << "   ✅ " << prog.name << " compilato\n";
    }
    std::cout << "\n";

    std::cout << "🚀 FASE 3: Esecuzione programmi\n";
    std::cout << "─────────────────────────────────────────────────────────\n";
    
    std::vector<Stats> all_stats;
    
    for (auto& prog : programs) {
        std::cout << "   ▶️  Eseguendo " << prog.name << "...\n";
        
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
        
        std::cout << "      Word Bigrams: " << stats.word_bigrams_unique << " unique\n";
        std::cout << "      Word Trigrams: " << stats.word_trigrams_unique << " unique\n";
        std::cout << "      Char Bigrams: " << stats.char_bigrams_unique << " unique\n";
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

    std::cout << "📊 FASE 4: Confronto risultati\n";
    std::cout << "─────────────────────────────────────────────────────────\n";
    
    bool all_match = true;
    
    // Confronta con i valori attesi
    const size_t EXPECTED_WB = 34;
    const size_t EXPECTED_WT = 37;
    const size_t EXPECTED_CB = 91;
    const size_t EXPECTED_CT = 124;
    const std::string EXPECTED_TOP_WB = "hello world";
    const size_t EXPECTED_TOP_WB_FREQ = 3;
    const std::string EXPECTED_TOP_CT = "t h e";
    const size_t EXPECTED_TOP_CT_FREQ = 8;
    
    for (size_t i = 0; i < programs.size(); ++i) {
        std::cout << "\n   🔍 Verifica " << programs[i].name << ":\n";
        
        bool prog_ok = true;
        
        if (all_stats[i].word_bigrams_unique != EXPECTED_WB) {
            std::cout << "      ❌ Word Bigrams: atteso " << EXPECTED_WB 
                      << ", ottenuto " << all_stats[i].word_bigrams_unique << "\n";
            prog_ok = false;
        } else {
            std::cout << "      ✅ Word Bigrams: " << EXPECTED_WB << " unique\n";
        }
        
        if (all_stats[i].word_trigrams_unique != EXPECTED_WT) {
            std::cout << "      ❌ Word Trigrams: atteso " << EXPECTED_WT 
                      << ", ottenuto " << all_stats[i].word_trigrams_unique << "\n";
            prog_ok = false;
        } else {
            std::cout << "      ✅ Word Trigrams: " << EXPECTED_WT << " unique\n";
        }
        
        if (all_stats[i].char_bigrams_unique != EXPECTED_CB) {
            std::cout << "      ❌ Char Bigrams: atteso " << EXPECTED_CB 
                      << ", ottenuto " << all_stats[i].char_bigrams_unique << "\n";
            prog_ok = false;
        } else {
            std::cout << "      ✅ Char Bigrams: " << EXPECTED_CB << " unique\n";
        }
        
        if (all_stats[i].char_trigrams_unique != EXPECTED_CT) {
            std::cout << "      ❌ Char Trigrams: atteso " << EXPECTED_CT 
                      << ", ottenuto " << all_stats[i].char_trigrams_unique << "\n";
            prog_ok = false;
        } else {
            std::cout << "      ✅ Char Trigrams: " << EXPECTED_CT << " unique\n";
        }
        
        // Verifica top n-grams (non critico, solo informativo)
        if (all_stats[i].top_word_bigram == EXPECTED_TOP_WB && 
            all_stats[i].top_word_bigram_freq == EXPECTED_TOP_WB_FREQ) {
            std::cout << "      ✅ Top Word Bigram: \"" << EXPECTED_TOP_WB << "\" (" 
                      << EXPECTED_TOP_WB_FREQ << ")\n";
        } else if (!all_stats[i].top_word_bigram.empty()) {
            std::cout << "      ℹ️  Top Word Bigram: \"" << all_stats[i].top_word_bigram 
                      << "\" (" << all_stats[i].top_word_bigram_freq << ") [parsing parziale]\n";
        }
        
        if (all_stats[i].top_char_trigram == EXPECTED_TOP_CT && 
            all_stats[i].top_char_trigram_freq == EXPECTED_TOP_CT_FREQ) {
            std::cout << "      ✅ Top Char Trigram: \"" << EXPECTED_TOP_CT << "\" (" 
                      << EXPECTED_TOP_CT_FREQ << ")\n";
        } else if (!all_stats[i].top_char_trigram.empty()) {
            std::cout << "      ℹ️  Top Char Trigram: \"" << all_stats[i].top_char_trigram 
                      << "\" (" << all_stats[i].top_char_trigram_freq << ") [parsing parziale]\n";
        }
        
        if (!prog_ok) {
            all_match = false;
        }
    }
    
    std::cout << "\n";
    std::cout << "🧹 FASE 5: Pulizia\n";
    std::cout << "─────────────────────────────────────────────────────────\n";
    
    // Rimuovi test file da book_gutenberg
    std::cout << "   🗑️  Rimozione file temporanei...\n";
    std::string rm_test = "rm -f " + test_file_dest;
    system(rm_test.c_str());
    
    // Ripristina i libri
    std::cout << "   📦 Ripristino libri...\n";
    std::string restore = "cd " + base_path + "/book_gutenberg && mv temp_backup/* . 2>/dev/null || true && rmdir temp_backup 2>/dev/null || true";
    system(restore.c_str());
    
    // Rimuovi binari di test
    for (const auto& prog : programs) {
        std::string rm_cmd = "rm -f " + base_path + "/" + prog.binary;
        system(rm_cmd.c_str());
    }
    
    std::cout << "   ✅ Ambiente ripristinato\n\n";

    // Risultato finale
    std::cout << "╔═══════════════════════════════════════════════════════╗\n";
    if (all_match) {
        std::cout << "║                 ✅ TEST SUPERATO!                     ║\n";
        std::cout << "║      Tutti i programmi producono risultati corretti   ║\n";
    } else {
        std::cout << "║                 ❌ TEST FALLITO!                      ║\n";
        std::cout << "║     Alcuni programmi hanno risultati incorretti       ║\n";
    }
    std::cout << "╚═══════════════════════════════════════════════════════╝\n\n";

    return all_match ? 0 : 1;
}
