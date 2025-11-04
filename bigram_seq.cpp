//
// Lorenzo Cappetti, 2025 - Sequential Version
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

namespace fs = std::filesystem;

// ==================== TEXT CLEANER ====================
// Classe che si occupa di rimuovere le parti indesiderate
// dei libri del Project Gutenberg (header, footer, indici, ecc.)
class TextCleaner {
public:
    // Rimuove l’intestazione Gutenberg (parte iniziale fino a "*** START OF..." o "CHAPTER")
    static std::string remove_gutenberg_header(const std::string& text) {
        std::string result = text;

        // Cerca "*** START OF" e taglia il testo dopo
        size_t start_pos = result.find("*** START OF");
        if (start_pos != std::string::npos) {
            size_t end_of_line = result.find("***", start_pos + 12);
            if (end_of_line != std::string::npos) {
                result = result.substr(end_of_line + 3);
            }
        }

        // Rimuove la sezione "Contents" fino al primo "CHAPTER"
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

    // Rimuove il footer Gutenberg (parte finale "*** END OF...")
    static std::string remove_gutenberg_footer(const std::string& text) {
        std::regex end_pattern(R"(\*\*\* END OF.*)", std::regex::icase);
        return std::regex_replace(text, end_pattern, "");
    }

    // Esegue la pulizia completa (header + footer)
    static std::string clean_text(const std::string& text) {
        std::string cleaned = remove_gutenberg_header(text);
        cleaned = remove_gutenberg_footer(cleaned);
        return cleaned;
    }
};

// ==================== TOKENIZER ====================
// Classe per normalizzare e tokenizzare il testo (in parole o caratteri)
class Tokenizer {
private:
    // Gestione dei caratteri UTF-8 (accenti, lettere speciali)
    static std::string process_utf8_char(const unsigned char* bytes, size_t& skip) {
        skip = 0;

        // Caratteri UTF-8 a 2 byte (accenti, lettere accentate)
        if ((bytes[0] & 0xE0) == 0xC0 && bytes[1]) {
            skip = 2;
            unsigned char first = bytes[0];
            unsigned char second = bytes[1];

            // Conversione dei caratteri accentati nelle lettere base (a, e, i, o, u, ecc.)
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

            return ""; // Rimuove altri caratteri UTF-8 non gestiti
        }

        // Caratteri UTF-8 a 3 byte (virgolette smart, ecc.)
        if ((bytes[0] & 0xF0) == 0xE0 && bytes[1] && bytes[2]) {
            skip = 3;
            if (bytes[0] == 0xE2 && bytes[1] == 0x80 &&
                (bytes[2] >= 0x98 && bytes[2] <= 0x9F)) return " ";
            return "";
        }

        // Caratteri a 4 byte (emoji, simboli) → rimossi
        if ((bytes[0] & 0xF8) == 0xF0 && bytes[1] && bytes[2] && bytes[3]) {
            skip = 4;
            return "";
        }

        return "";
    }

public:
    // Normalizza il testo (minuscole, rimozione numeri e punteggiatura)
    static std::string normalize(const std::string& text, bool remove_punct = false) {
        std::string result;
        result.reserve(text.size());

        for (size_t i = 0; i < text.size(); ++i) {
            unsigned char c = static_cast<unsigned char>(text[i]);

            // Gestisce caratteri multibyte
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

            // Rimuove numeri
            if (std::isdigit(c)) continue;

            // Rimuove punteggiatura se richiesto
            if (remove_punct && std::ispunct(c)) {
                result += ' ';
                continue;
            }

            // Converte in minuscolo solo lettere e spazi
            if (std::isalpha(c) || std::isspace(c))
                result += std::tolower(c);
        }
        return result;
    }

    // Tokenizza in parole
    static std::vector<std::string> tokenize_words(const std::string& text) {
        std::vector<std::string> tokens;
        std::istringstream iss(text);
        std::string word;
        while (iss >> word) {
            if (!word.empty()) tokens.push_back(word);
        }
        return tokens;
    }

    // Tokenizza in caratteri (ignorando spazi)
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
// Classe template per estrarre n-grammi da un vettore di token (parole o caratteri)
template<typename T>
class NgramExtractor {
public:
    // Estrae tutti gli n-grammi di lunghezza n
    static std::vector<std::vector<T>> extract(const std::vector<T>& tokens, size_t n) {
        std::vector<std::vector<T>> ngrams;
        if (tokens.size() < n) return ngrams;
        for (size_t i = 0; i <= tokens.size() - n; ++i) {
            ngrams.emplace_back(tokens.begin() + i, tokens.begin() + i + n);
        }
        return ngrams;
    }

    // Converte un n-gram in stringa (unita da spazi)
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
// Classe per contare le frequenze degli n-grammi
class FrequencyCounter {
private:
    std::unordered_map<std::string, size_t> frequencies;

public:
    // Incrementa la frequenza di un n-gram
    void add_ngram(const std::string& ngram) {
        frequencies[ngram]++;
    }

    // Unisce un altro dizionario di frequenze
    void merge(const std::unordered_map<std::string, size_t>& other) {
        for (const auto& [ngram, count] : other) {
            frequencies[ngram] += count;
        }
    }

    // Restituisce i top-N n-gram più frequenti
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
// Classe che gestisce il flusso di analisi sequenziale di un testo
class SequentialProcessor {
public:
    static void process_text_sequential(
        const std::string& text,
        size_t n,
        bool use_words,
        FrequencyCounter& counter
    ) {
        // Normalizza il testo (minuscole, niente punteggiatura)
        std::string normalized = Tokenizer::normalize(text, true);

        // Se analisi per parole
        if (use_words) {
            auto tokens = Tokenizer::tokenize_words(normalized);
            auto ngrams = NgramExtractor<std::string>::extract(tokens, n);
            for (const auto& ng : ngrams)
                counter.add_ngram(NgramExtractor<std::string>::ngram_to_string(ng));
        }
        // Se analisi per caratteri
        else {
            auto tokens = Tokenizer::tokenize_chars(normalized);
            auto ngrams = NgramExtractor<char>::extract(tokens, n);
            for (const auto& ng : ngrams)
                counter.add_ngram(NgramExtractor<char>::ngram_to_string(ng));
        }
    }
};

// ==================== STATISTICS GENERATOR ====================
// Classe per stampare e salvare le statistiche sugli n-grammi
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

    // Esporta i risultati in un file CSV
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
// Punto di ingresso del programma
int main() {
    std::cout << "Sequential N-gram Analyzer for Multiple Books\n";
    std::cout << "==============================================\n\n";

    // Percorso della cartella contenente i libri .txt
    std::string folder_path = "/Users/lorenzocappetti/CLionProjects/Bigrams_Trigrams/book_gutenberg";

    // Verifica che la cartella esista
    if (!fs::exists(folder_path) || !fs::is_directory(folder_path)) {
        std::cerr << "Errore: cartella '" << folder_path << "' non trovata!\n";
        return 1;
    }

    // Colleziona tutti i file .txt nella cartella
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

    // Inizializza i contatori globali
    FrequencyCounter word_bigrams, word_trigrams, char_bigrams, char_trigrams;

    auto start_time = std::chrono::high_resolution_clock::now();

    // Processa ogni libro in sequenza
    for (size_t i = 0; i < book_files.size(); ++i) {
        const auto& filepath = book_files[i];
        std::cout << "[" << (i+1) << "/" << book_files.size() << "] Processando: "
                  << fs::path(filepath).filename().string() << "... ";
        std::cout.flush();

        // Legge il contenuto del file
        std::ifstream file(filepath);
        if (!file) {
            std::cerr << "Impossibile aprire!\n";
            continue;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string text = buffer.str();

        // Rimuove intestazioni e footer di Gutenberg
        text = TextCleaner::clean_text(text);

        // Calcola bigrammi e trigrammi per parole e caratteri
        SequentialProcessor::process_text_sequential(text, 2, true, word_bigrams);
        SequentialProcessor::process_text_sequential(text, 3, true, word_trigrams);
        SequentialProcessor::process_text_sequential(text, 2, false, char_bigrams);
        SequentialProcessor::process_text_sequential(text, 3, false, char_trigrams);

        std::cout << "OK\n";
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end_time - start_time;

    // Stampa le statistiche a video
    StatisticsGenerator::print_statistics(word_bigrams, "Word Bigrams", 2);
    StatisticsGenerator::print_statistics(word_trigrams, "Word Trigrams", 3);
    StatisticsGenerator::print_statistics(char_bigrams, "Char Bigrams", 2);
    StatisticsGenerator::print_statistics(char_trigrams, "Char Trigrams", 3);

    std::cout << "\nTempo totale di esecuzione: " << elapsed.count() << " secondi\n";

    // Salva le statistiche su file CSV
    StatisticsGenerator::save_to_file(word_bigrams, "word_bigrams.csv");
    StatisticsGenerator::save_to_file(word_trigrams, "word_trigrams.csv");
    StatisticsGenerator::save_to_file(char_bigrams, "char_bigrams.csv");
    StatisticsGenerator::save_to_file(char_trigrams, "char_trigrams.csv");

    return 0;
}
