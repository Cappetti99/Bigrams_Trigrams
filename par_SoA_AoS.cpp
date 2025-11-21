//
// Lorenzo Cappetti, 2025 - Hybrid AoS/SoA
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
#include <numeric>
#include <cmath>
#include <ctime>

namespace fs = std::filesystem;

//═══════════════════════════════���═══════════════════════════════
// UTILITY FUNCTIONS
//═══════════════════════════════════════════════════════════════

/**
 * Crea una directory se non esiste già.
 * @param dir Path della directory da creare
 */
static void ensure_directory_exists(const std::string& dir) {
    if (!fs::exists(dir)) {
        fs::create_directories(dir);
    }
}

//═══════════════════════════════════════════════════════════════
// TEXT CLEANER
// Rimuove header e footer standard dei libri Project Gutenberg
//═══════════════════════════════════════════════════════════════
class TextCleaner {
public:
    /**
     * Rimuove l'header standard di Project Gutenberg (tutto prima di "*** START OF")
     * @param text Testo completo del libro
     * @return Testo senza header
     */
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

    /**
     * Rimuove il footer standard di Project Gutenberg (tutto dopo "*** END OF")
     * @param text Testo completo del libro
     * @return Testo senza footer
     */
    static std::string remove_gutenberg_footer(const std::string& text) {
        size_t end_pos = text.find("*** END OF");
        if (end_pos != std::string::npos) {
            return text.substr(0, end_pos);
        }
        return text;
    }

    /**
     * Applica entrambe le pulizie (header + footer)
     * @param text Testo grezzo
     * @return Testo pulito contenente solo il contenuto del libro
     */
    static std::string clean_text(const std::string& text) {
        std::string cleaned = remove_gutenberg_header(text);
        cleaned = remove_gutenberg_footer(cleaned);
        return cleaned;
    }
};

//═══════════════════════════════════════════════════════════════
// TOKENIZER
// Normalizza e tokenizza il testo in parole o caratteri
//═══════════════════════════════════════════════════════════════
class Tokenizer {
private:
    /**
     * Genera una lookup table per la conversione rapida lowercase.
     * Evita chiamate ripetute a tolower() per ogni carattere.
     * @return Puntatore a tabella statica di conversione
     */
    static const unsigned char* get_to_lower() {
        // Tabella statica 256 elementi: per ogni byte, il suo equivalente lowercase
        static unsigned char table[256];
        static bool initialized = false;
        if (!initialized) {
            for (int i = 0; i < 256; i++) {
                // Se è A-Z, converte in a-z (ASCII +32), altrimenti lascia invariato
                table[i] = (i >= 'A' && i <= 'Z') ? i + 32 : i;
            }
            initialized = true;
        }
        return table;
    }

    /**
     * Gestisce caratteri UTF-8 a 2 byte (accenti europei: à, é, ñ, ecc.)
     * Li converte nelle rispettive versioni senza accenti.
     * @param bytes Puntatore ai byte UTF-8
     * @param skip Numero di byte da saltare dopo questo carattere
     * @return Carattere normalizzato (senza accento) o stringa vuota se da ignorare
     */
    static inline std::string process_utf8_char(const unsigned char* bytes, size_t& skip) {
        skip = 0;

        // Caratteri a 2 byte: 110xxxxx 10xxxxxx
        if ((bytes[0] & 0xE0) == 0xC0 && bytes[1]) {
            skip = 2;
            unsigned char first = bytes[0];
            unsigned char second = bytes[1];

            // Latin-1 Supplement (0xC3): contiene à, é, ì, ò, ù, ñ, ç, ecc.
            if (first == 0xC3) {
                // à á â ã ä å → a (range 0x80-0x85 lowercase, 0xA0-0xA5 uppercase)
                if ((second >= 0x80 && second <= 0x85) || (second >= 0xA0 && second <= 0xA5)) return "a";
                // è é ê ë → e
                if ((second >= 0x88 && second <= 0x8B) || (second >= 0xA8 && second <= 0xAB)) return "e";
                // ì í î ï → i
                if ((second >= 0x8C && second <= 0x8F) || (second >= 0xAC && second <= 0xAF)) return "i";
                // ò ó ô õ ö → o
                if ((second >= 0x92 && second <= 0x96) || (second >= 0xB2 && second <= 0xB6)) return "o";
                // ù ú û ü → u
                if ((second >= 0x99 && second <= 0x9C) || (second >= 0xB9 && second <= 0xBC)) return "u";
                // ñ → n
                if (second == 0x91 || second == 0xB1) return "n";
                // ç → c
                if (second == 0x87 || second == 0xA7) return "c";
            }
            return "";  // Altri caratteri a 2 byte vengono ignorati
        }

        // Skippa caratteri multi-byte non gestiti (emoji, simboli speciali, ecc.)
        if (bytes[0] >= 0x80) {
            skip = 1;
            // Conta quanti byte di continuation (10xxxxxx) seguono
            while (skip < 4 && bytes[skip] && (bytes[skip] & 0xC0) == 0x80) skip++;
            return "";
        }

        return "";
    }

public:
    /**
     * Normalizza il testo: lowercase, rimuove numeri, gestisce UTF-8, opzionalmente rimuove punteggiatura.
     * @param text Testo grezzo
     * @param remove_punct Se true, sostituisce la punteggiatura con spazi
     * @return Testo normalizzato
     */
    static std::string normalize(const std::string& text, bool remove_punct = false) {
        std::string result;
        result.reserve(text.size());
        const unsigned char* to_lower = get_to_lower();

        for (size_t i = 0; i < text.size(); ++i) {
            unsigned char c = static_cast<unsigned char>(text[i]);

            // Gestione caratteri UTF-8 (non-ASCII)
            if (c >= 0x80) {
                size_t skip;
                std::string replacement = process_utf8_char(
                    reinterpret_cast<const unsigned char*>(&text[i]), skip);
                if (skip > 0) {
                    result += replacement;
                    i += skip - 1;  // Salta i byte già processati
                    continue;
                }
            }

            // Rimuove numeri completamente
            if (std::isdigit(c)) continue;

            // Punteggiatura → spazio (se richiesto)
            if (remove_punct && std::ispunct(c)) {
                result += ' ';
                continue;
            }

            // Lettere → lowercase, spazi → spazi
            if (std::isalpha(c)) {
                result += to_lower[c];
            } else if (std::isspace(c)) {
                result += ' ';
            }
        }
        return result;
    }

    /**
     * Divide il testo in parole (tokenizzazione word-based).
     * @param text Testo normalizzato
     * @param tokens Vettore output contenente le parole
     */
    static void tokenize_words(const std::string& text, std::vector<std::string>& tokens) {
        tokens.clear();
        std::istringstream iss(text);
        std::string word;
        while (iss >> word) {  // Split su spazi
            if (!word.empty()) {
                tokens.push_back(std::move(word));
            }
        }
    }

    /**
     * Estrae tutti i caratteri non-spazio (tokenizzazione char-based).
     * @param text Testo normalizzato
     * @param chars Vettore output contenente i caratteri
     */
    static void tokenize_chars(const std::string& text, std::vector<char>& chars) {
        chars.clear();
        for (char c : text) {
            if (!std::isspace(static_cast<unsigned char>(c))) {
                chars.push_back(c);
            }
        }
    }
};

//═══════════════════════════════════════════════════════════════
// OPTIMIZED STRING POOL
// Arena allocator per string interning: memorizza ogni parola unica
// una sola volta e assegna un ID numerico. Riduce uso memoria e
// velocizza confronti (confronto di size_t invece di std::string).
//════════════��══════════════════════════════════════════════════
class OptimizedStringPool {
private:
    std::vector<char> arena;                                  // Buffer contiguo per tutte le stringhe
    std::vector<std::string_view> id_to_word;                 // Mappa ID → parola
    std::unordered_map<std::string_view, size_t> word_to_id;  // Mappa parola → ID

public:
    OptimizedStringPool() {
        // Pre-alloca 10MB per evitare riallocazioni durante l'inserimento
        arena.reserve(10'000'000);
    }

    /**
     * Registra una parola nel pool e restituisce il suo ID univoco.
     * Se la parola esiste già, restituisce l'ID esistente.
     * @param s Parola da internare
     * @return ID univoco della parola
     */
    size_t intern(std::string_view s) {
        // Controlla se la parola esiste già
        auto it = word_to_id.find(s);
        if (it != word_to_id.end()) {
            return it->second;
        }

        // Inserisce la parola nell'arena
        size_t pos = arena.size();
        arena.insert(arena.end(), s.begin(), s.end());

        // Crea una view stabile (punta nell'arena)
        std::string_view view(arena.data() + pos, s.size());
        size_t new_id = id_to_word.size();
        id_to_word.push_back(view);
        word_to_id[view] = new_id;
        return new_id;
    }

    /**
     * Recupera la parola corrispondente a un ID.
     * @param id ID della parola
     * @return String view della parola
     */
    std::string_view get(size_t id) const {
        return id_to_word[id];
    }

    /**
     * @return Numero di parole uniche nel pool
     */
    size_t size() const { return id_to_word.size(); }
};

//═══════════════════════���═══════════════════════���═══════════════
// NGRAM ID
// Rappresenta un n-gram come array di ID di parole (invece di stringhe).
// Dimensione fissa: massimo 3 parole (per trigrammi).
// Nessuna allocazione dinamica → molto più veloce.
//═══════════════════════════════���═══════════════════════════════
struct NgramID {
    size_t word_ids[3];  // Array fisso di ID parole (max 3 per trigrammi)
    size_t length;       // Lunghezza effettiva (2 per bigrammi, 3 per trigrammi)

    /**
     * Confronto di uguaglianza tra n-gram.
     * Usato dalla unordered_map per risolvere collisioni hash.
     */
    bool operator==(const NgramID& other) const noexcept {
        if (length != other.length) return false;
        for (size_t i = 0; i < length; ++i) {
            if (word_ids[i] != other.word_ids[i]) return false;
        }
        return true;
    }
};

/**
 * Hash function per NgramID.
 * Combina hash di ogni word_id usando XOR e rotazioni bit.
 */
struct NgramIDHash {
    size_t operator()(const NgramID& n) const noexcept {
        size_t hash = n.length;
        for (size_t i = 0; i < n.length; ++i) {
            // FNV-like hash mixing: XOR + moltiplicazione + bit shifting
            hash ^= (n.word_ids[i] * 2654435761ULL) + (hash << 6) + (hash >> 2);
        }
        return hash;
    }
};

//═══════════════════════════════════════════════════════════════
// HYBRID FREQUENCY COUNTER
// Struttura dati ibrida:
//   - Fase di BUILD (AoS): converte da unordered_map<string, size_t>
//   - Fase di QUERY (SoA): array paralleli per cache locality ottimale
//═══════════════════════════════════════════════���═══════════════
class HybridFrequencyCounter {
private:
    OptimizedStringPool pool;           // Pool condiviso per string interning
    std::vector<NgramID> ngram_ids;     // Array di n-gram (come ID)
    std::vector<size_t> frequencies;    // Array parallelo delle frequenze
    bool finalized = false;             // Flag: true dopo build_from_aos()

public:
    /**
     * Costruisce la struttura SoA a partire da una AoS map.
     * Converte stringhe in ID numerici e crea array paralleli.
     * @param aos_map Mappa <ngram_string, frequenza> da convertire
     */
    void build_from_aos(const std::unordered_map<std::string, size_t>& aos_map) {
        size_t total_size = aos_map.size();

        // Pre-alloca per evitare riallocazioni
        ngram_ids.clear();
        frequencies.clear();
        ngram_ids.reserve(total_size);
        frequencies.reserve(total_size);

        // Converte ogni n-gram string in NgramID
        for (const auto& [ngram_str, freq] : aos_map) {
            NgramID id;
            id.length = 0;

            // Parsing manuale (più veloce di istringstream)
            size_t start = 0;
            size_t pos = 0;

            // Split su spazi, max 3 parole
            while (pos <= ngram_str.size() && id.length < 3) {
                if (pos == ngram_str.size() || ngram_str[pos] == ' ') {
                    if (pos > start) {
                        std::string_view word(&ngram_str[start], pos - start);
                        id.word_ids[id.length++] = pool.intern(word);  // Interna la parola
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

    /**
     * Restituisce i top N n-gram per frequenza.
     * Usa partial_sort per evitare di ordinare tutto l'array.
     * @param n Numero di top risultati richiesti
     * @return Vettore di coppie <ngram_string, frequenza> ordinato per frequenza decrescente
     */
    std::vector<std::pair<std::string, size_t>> get_top_n(size_t n) const {
        if (!finalized) {
            throw std::runtime_error("Devi chiamare build_from_aos() prima!");
        }

        // Crea vettore di indici [0, 1, 2, ..., N-1]
        std::vector<size_t> indices(frequencies.size());
        std::iota(indices.begin(), indices.end(), 0);

        size_t k = std::min(n, indices.size());

        // Partial sort: ordina solo i primi K elementi (O(N log K) invece di O(N log N))
        std::partial_sort(
            indices.begin(),
            indices.begin() + k,
            indices.end(),
            [this](size_t i, size_t j) {
                return frequencies[i] > frequencies[j];  // Ordine decrescente
            }
        );

        // Ricostruisce le stringhe per i top K
        std::vector<std::pair<std::string, size_t>> result;
        result.reserve(k);

        for (size_t i = 0; i < k; ++i) {
            size_t idx = indices[i];
            const auto& id = ngram_ids[idx];

            // Concatena le parole con spazi
            std::string ngram;
            for (size_t j = 0; j < id.length; ++j) {
                if (j > 0) ngram += " ";
                ngram += std::string(pool.get(id.word_ids[j]));
            }

            result.emplace_back(std::move(ngram), frequencies[idx]);
        }

        return result;
    }

    /**
     * Restituisce TUTTI gli n-gram ordinati per frequenza.
     * @return Vettore completo ordinato per frequenza decrescente
     */
    std::vector<std::pair<std::string, size_t>> get_all_sorted() const {
        if (!finalized) {
            throw std::runtime_error("Devi chiamare build_from_aos() prima!");
        }

        std::vector<size_t> indices(frequencies.size());
        std::iota(indices.begin(), indices.end(), 0);

        // Full sort
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

    /**
     * @return Numero di n-gram unici
     */
    size_t total_unique() const { return ngram_ids.size(); }

    /**
     * @return Somma di tutte le frequenze (numero totale di occorrenze)
     */
    size_t total_count() const {
        return std::accumulate(frequencies.begin(), frequencies.end(), 0ULL);
    }
};

//═══════════════════════════════���═══════════════════════════════
// CSV SAVER
// Salva i risultati in formato CSV
//═══════════════════════════════════════════════════════════════
class CSVSaver {
public:
    /**
     * Salva tutti gli n-gram in un file CSV ordinati per frequenza.
     * @param counter Struttura SoA contenente gli n-gram
     * @param filename Path del file CSV output
     * @param label Label descrittiva per il messaggio di conferma
     */
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

        // Header CSV
        out << "ngram,frequency\n";

        // Dati (n-gram tra virgolette per gestire spazi)
        for (const auto& [ngram, freq] : all_ngrams) {
            out << "\"" << ngram << "\"," << freq << "\n";
        }

        out.close();
        std::cout << "💾 " << label << ": " << all_ngrams.size()
                  << " n-grams → " << filename << "\n";
    }
};

//═══════════════════════════════════════════════════════════════
// OPTIMIZED OPENMP PROCESSOR
// Gestisce il processing parallelo in 3 fasi:
//   1. PHASE 1: Processing parallelo (ogni thread ha la sua AoS map)
//   2. PHASE 2: Merge parallelo delle map (merge tree)
//   3. PHASE 3: Conversione AoS → SoA parallela (#pragma omp sections)
//═══════════════════════════════���═══════════════════════���═══════
class OptimizedOpenMPProcessor {
public:
    /**
     * Processa un singolo testo ed estrae tutti i tipi di n-gram.
     * Versione ottimizzata con concatenazione diretta invece di ostringstream.
     * @param text Testo normalizzato
     * @param word_bigrams Map output per bigrammi di parole
     * @param word_trigrams Map output per trigrammi di parole
     * @param char_bigrams Map output per bigrammi di caratteri
     * @param char_trigrams Map output per trigrammi di caratteri
     * @param words_buffer Buffer riutilizzabile per tokenizzazione parole
     * @param chars_buffer Buffer riutilizzabile per tokenizzazione caratteri
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

    /**
     * Merge parallelo di map thread-local in una map globale.
     * Algoritmo: merge tree binario (sempre mappa piccola → mappa grande).
     * Complessità: O(log T) passi paralleli, dove T = num_threads.
     * @param thread_maps Vettore di map thread-local
     * @param result Map globale risultante
     */
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

    /**
     * PIPELINE COMPLETA: processing parallelo → merge → conversione SoA.
     * @param book_files Vettore di path ai file .txt
     * @param word_bigrams Output SoA per bigrammi di parole
     * @param word_trigrams Output SoA per trigrammi di parole
     * @param char_bigrams Output SoA per bigrammi di caratteri
     * @param char_trigrams Output SoA per trigrammi di caratteri
     * @param num_threads Numero di thread OpenMP (0 = auto)
     */
    static void process_parallel_hybrid(
        const std::vector<std::string>& book_files,
        HybridFrequencyCounter& word_bigrams,
        HybridFrequencyCounter& word_trigrams,
        HybridFrequencyCounter& char_bigrams,
        HybridFrequencyCounter& char_trigrams,
        int num_threads = 0
    ) {
        if (num_threads == 0) num_threads = omp_get_max_threads();
        omp_set_num_threads(num_threads);

        int total_books = book_files.size();

        // ========== PHASE 1: PARALLEL PROCESSING ==========
        // Ogni thread ha 4 map locali (word_bigrams, word_trigrams, char_bigrams, char_trigrams)
        std::vector<std::unordered_map<std::string, size_t>> thread_wb(num_threads);
        std::vector<std::unordered_map<std::string, size_t>> thread_wt(num_threads);
        std::vector<std::unordered_map<std::string, size_t>> thread_cb(num_threads);
        std::vector<std::unordered_map<std::string, size_t>> thread_ct(num_threads);

        auto phase1_start = std::chrono::high_resolution_clock::now();

        #pragma omp parallel default(none) shared(book_files, total_books, thread_wb, thread_wt, thread_cb, thread_ct)
        {
            int tid = omp_get_thread_num();
            auto& my_wb = thread_wb[tid];
            auto& my_wt = thread_wt[tid];
            auto& my_cb = thread_cb[tid];
            auto& my_ct = thread_ct[tid];

            // Pre-allocazione intelligente per ridurre rehashing
            my_wb.reserve(100000);   // Stima bigrammi parole
            my_wt.reserve(200000);   // Stima trigrammi parole (più numerosi)
            my_cb.reserve(50000);    // Stima bigrammi caratteri
            my_ct.reserve(100000);   // Stima trigrammi caratteri

            // Buffer riutilizzabili per tokenizzazione (evita allocazioni ripetute)
            std::vector<std::string> words_buf;
            std::vector<char> chars_buf;
            words_buf.reserve(10000);
            chars_buf.reserve(50000);

            // Dynamic scheduling: distribuisce i libri dinamicamente (load balancing)
            #pragma omp for schedule(dynamic) nowait
            for (int i = 0; i < total_books; ++i) {
                const auto& filepath = book_files[i];

                // Lettura file ottimizzata: binary + ate (vai alla fine per ottenere dimensione)
                std::ifstream file(filepath, std::ios::binary | std::ios::ate);
                if (!file) continue;

                std::streamsize size = file.tellg();
                file.seekg(0, std::ios::beg);
                std::string text(size, '\0');
                if (!file.read(&text[0], size)) continue;

                // Pulizia header/footer Gutenberg
                text = TextCleaner::clean_text(text);

                // Estrazione n-gram
                process_text_aos_optimized(text, my_wb, my_wt, my_cb, my_ct,
                                          words_buf, chars_buf);
            }
        }

        auto phase1_end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> phase1_time = phase1_end - phase1_start;

        // ========== PHASE 2: PARALLEL MERGE ==========
        auto phase2_start = std::chrono::high_resolution_clock::now();

        std::unordered_map<std::string, size_t> merged_wb, merged_wt, merged_cb, merged_ct;

        // Merge parallelo delle 4 categorie di n-gram (eseguito in sequenza tra categorie)
        parallel_merge_aos(thread_wb, merged_wb);
        parallel_merge_aos(thread_wt, merged_wt);
        parallel_merge_aos(thread_cb, merged_cb);
        parallel_merge_aos(thread_ct, merged_ct);

        auto phase2_end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> phase2_time = phase2_end - phase2_start;

        // ========== PHASE 3: PARALLEL AOS → SOA CONVERSION ==========
        auto phase3_start = std::chrono::high_resolution_clock::now();

        // Converte le 4 map AoS in 4 strutture SoA in parallelo
        #pragma omp parallel sections default(none) shared(word_bigrams, word_trigrams, char_bigrams, char_trigrams, merged_wb, merged_wt, merged_cb, merged_ct)
        {
            #pragma omp section
            word_bigrams.build_from_aos(merged_wb);

            #pragma omp section
            word_trigrams.build_from_aos(merged_wt);

            #pragma omp section
            char_bigrams.build_from_aos(merged_cb);

            #pragma omp section
            char_trigrams.build_from_aos(merged_ct);
        }

        auto phase3_end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> phase3_time = phase3_end - phase3_start;
    }
};

//═══════════════════════���═══════════════════════════════════════
// MAIN
//═══════════════════════════════════════════════════════════════
int main() {
    std::cout << "\n╔═══════════════════════════════════════════════���═══════╗\n";
    std::cout << "║   Hybrid AoS/SoA N-gram Analyzer (ULTRA-OPTIMIZED)   ║\n";
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

    std::cout << "📚 Trovati " << book_files.size() << " libri\n";

    int max_threads = omp_get_max_threads();
    const int MAX_VIRTUAL_THREADS = 32;
    int num_threads;

    std::cout << "🧵 Thread disponibili: " << max_threads << " (max virtuale: " << MAX_VIRTUAL_THREADS << ")\n";
    std::cout << "Quanti thread vuoi usare? (1-" << MAX_VIRTUAL_THREADS << "): ";
    std::cout.flush();

    while (true) {
        std::cin >> num_threads;

        if (std::cin.fail()) {
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            std::cout << "⚠️  Inserisci un numero valido (1-" << MAX_VIRTUAL_THREADS << "): ";
            continue;
        }

        if (num_threads < 1 || num_threads > MAX_VIRTUAL_THREADS) {
            std::cout << "⚠️  Fuori range! Inserisci un valore tra 1 e " << MAX_VIRTUAL_THREADS << " : ";
            continue;
        }

        break;
    }

    if (num_threads > max_threads) {
        std::cout << "⚡ Usando " << num_threads << " thread VIRTUALI (oltre i " << max_threads << " fisici)\n\n";
    } else {
        std::cout << "✅ Usando " << num_threads << " thread fisici\n\n";
    }

    const int WARMUP_RUNS = 2;  // Prime run da scartare per warm-up CPU/cache
    const int MEASURED_RUNS = 10;  // Run effettive da misurare
    const int NUM_RUNS = WARMUP_RUNS + MEASURED_RUNS;  // Totale: 12 run

    struct RunResult { double wall; double cpu; bool warmup; };
    std::vector<RunResult> run_times;
    run_times.reserve(NUM_RUNS);

    std::cout << "🔄 Eseguendo " << NUM_RUNS << " run totali (" << WARMUP_RUNS
              << " warm-up + " << MEASURED_RUNS << " misurate)...\n";

    HybridFrequencyCounter word_bigrams, word_trigrams, char_bigrams, char_trigrams;

    for (int run = 0; run < NUM_RUNS; ++run) {
        word_bigrams = HybridFrequencyCounter();
        word_trigrams = HybridFrequencyCounter();
        char_bigrams = HybridFrequencyCounter();
        char_trigrams = HybridFrequencyCounter();

        auto start_time = std::chrono::high_resolution_clock::now();
        std::clock_t start_cpu = std::clock();

        OptimizedOpenMPProcessor::process_parallel_hybrid(
            book_files,
            word_bigrams,
            word_trigrams,
            char_bigrams,
            char_trigrams,
            num_threads
        );

        auto end_time = std::chrono::high_resolution_clock::now();
        std::clock_t end_cpu = std::clock();
        std::chrono::duration<double> elapsed = end_time - start_time;
        double cpu_seconds = double(end_cpu - start_cpu) / double(CLOCKS_PER_SEC);

        run_times.push_back(RunResult{elapsed.count(), cpu_seconds, run < WARMUP_RUNS});

        std::cout << "  Run " << std::setw(2) << (run + 1) << "/" << NUM_RUNS;
        if (run < WARMUP_RUNS) {
            std::cout << " [WARM-UP]: ";
        } else {
            std::cout << ": ";
        }
        std::cout << std::fixed << std::setprecision(2) << elapsed.count()
                  << "s (cpu: " << cpu_seconds << "s)\n";
    }

    // ==================== CALCOLO STATISTICHE (SCARTANDO WARM-UP) ====================
    std::vector<double> measured_times;
    std::vector<double> measured_cpu;
    for (const auto& r : run_times) {
        if (!r.warmup) {
            measured_times.push_back(r.wall);
            measured_cpu.push_back(r.cpu);
        }
    }

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
    double cv = (stddev / mean) * 100.0;

    double mean_cpu = 0.0, min_cpu = measured_cpu[0], max_cpu = measured_cpu[0];
    for (double t : measured_cpu) {
        mean_cpu += t;
        min_cpu = std::min(min_cpu, t);
        max_cpu = std::max(max_cpu, t);
    }
    mean_cpu /= measured_cpu.size();

    double stddev_cpu = 0.0;
    for (double t : measured_cpu) stddev_cpu += (t - mean_cpu) * (t - mean_cpu);
    stddev_cpu = std::sqrt(stddev_cpu / measured_cpu.size());
    double cv_cpu = (stddev_cpu / mean_cpu) * 100.0;

    // ==================== SALVA REPORT PERFORMANCE SU FILE ====================
    std::string output_dir = "test/output_hybrid";
    ensure_directory_exists(output_dir);

    std::string perf_report = output_dir + "/performance_report_hybrid_T" + std::to_string(num_threads) + ".txt";
    std::ofstream report_file(perf_report);
    if (report_file) {
        report_file << "═══════════════════════════════════════════════════════���═══════\n";
        report_file << "  PERFORMANCE METRICS REPORT - HYBRID AoS/SoA ANALYZER\n";
        report_file << "  Lorenzo Cappetti, 2025\n";
        report_file << "  Generato: " << __DATE__ << " " << __TIME__ << "\n";
        report_file << "═══════════════════════════════════════════════���═══════════════\n\n";

        report_file << "1. CONFIGURAZIONE ESECUZIONE\n";
        report_file << "   - Numero libri processati:  " << book_files.size() << "\n";
        report_file << "   - Thread utilizzati:        " << num_threads << "\n";
        report_file << "   - Thread fisici disponibili:" << max_threads << "\n";
        report_file << "   - Run totali:               " << NUM_RUNS << "\n";
        report_file << "   - Run warm-up (scartate):   " << WARMUP_RUNS << "\n";
        report_file << "   - Run misurate:             " << MEASURED_RUNS << "\n";
        report_file << "   - Versione:                 Hybrid AoS/SoA (ULTRA-OPTIMIZED)\n\n";

        report_file << "2. EXECUTION TIME METRICS (Wall-Clock)\n";
        report_file << "   ┌─────────────────────────────────────────────────┐\n";
        report_file << "   │ Media:           " << std::setw(10) << std::fixed << std::setprecision(3)
               << mean << " s                   │\n";
        report_file << "   │ Minimo:          " << std::setw(10) << min_time << " s                   │\n";
        report_file << "   │ Massimo:         " << std::setw(10) << max_time << " s                   │\n";
        report_file << "   │ Deviazione Std:  " << std::setw(10) << stddev << " s                   │\n";
        report_file << "   │ Coeff. Variaz.:  " << std::setw(9) << std::setprecision(2)
               << cv << " %                    │\n";
        report_file << "   └─────────────────────────────────────────────────┘\n\n";

        report_file << "3. CPU TIME METRICS (somma di tutti i thread)\n";
        report_file << "   ┌─────────────────────────────────────────────────┐\n";
        report_file << "   │ Media:           " << std::setw(10) << std::fixed << std::setprecision(3)
               << mean_cpu << " s                   │\n";
        report_file << "   │ Minimo:          " << std::setw(10) << min_cpu << " s                   │\n";
        report_file << "   │ Massimo:         " << std::setw(10) << max_cpu << " s                   │\n";
        report_file << "   │ Deviazione Std:  " << std::setw(10) << stddev_cpu << " s                   │\n";
        report_file << "   │ Coeff. Variaz.:  " << std::setw(9) << std::setprecision(2)
               << cv_cpu << " %                    │\n";
        report_file << "   └─────────────────────────────────────────────────┘\n\n";

        double parallelism_ratio = mean_cpu / mean;
        report_file << "4. PARALLELISMO E EFFICIENZA\n";
        report_file << "   - Parallelism ratio (CPU/Wall):  " << std::fixed << std::setprecision(2)
               << parallelism_ratio << "x\n";
        report_file << "   - Efficienza parallela:          " << std::setprecision(1)
               << (parallelism_ratio / num_threads * 100.0) << "%\n";
        report_file << "   - Speedup teorico massimo:       " << num_threads << "x\n";
        report_file << "   - Speedup effettivo (vs seq):    da calcolare dopo run sequenziale\n\n";

        report_file << "5. DETTAGLIO RUN INDIVIDUALI\n";
        report_file << "   Run  Warmup  Wall-Time(s)  CPU-Time(s)  Parallel-Ratio\n";
        report_file << "   ───  ──────  ────────────  ───────────  ──────────────\n";
        for (size_t i = 0; i < run_times.size(); ++i) {
            double ratio = run_times[i].cpu / run_times[i].wall;
            report_file << "   " << std::setw(3) << (i+1) << "    "
                   << (run_times[i].warmup ? "YES" : " NO") << "    "
                   << std::setw(10) << std::fixed << std::setprecision(3) << run_times[i].wall << "    "
                   << std::setw(10) << run_times[i].cpu << "      "
                   << std::setw(6) << std::setprecision(2) << ratio << "x\n";
        }
        report_file << "\n";

        report_file << "6. STABILITÀ E AFFIDABILITÀ\n";
        report_file << "   - Variabilità wall-clock:  ";
        if (cv < 2.0) report_file << "ECCELLENTE (< 2%)\n";
        else if (cv < 5.0) report_file << "BUONA (< 5%)\n";
        else if (cv < 10.0) report_file << "ACCETTABILE (< 10%)\n";
        else report_file << "ALTA (≥ 10%)\n";

        report_file << "   - Variabilità CPU-time:    ";
        if (cv_cpu < 2.0) report_file << "ECCELLENTE (< 2%)\n";
        else if (cv_cpu < 5.0) report_file << "BUONA (< 5%)\n";
        else if (cv_cpu < 10.0) report_file << "ACCETTABILE (< 10%)\n";
        else report_file << "ALTA (≥ 10%)\n\n";

        report_file << "7. NOTE\n";
        report_file << "   - Le prime " << WARMUP_RUNS << " run sono state scartate per warm-up CPU/cache\n";
        report_file << "   - Tutte le statistiche sono calcolate solo sulle " << MEASURED_RUNS << " run misurate\n";
        report_file << "   - CPU time è la somma del tempo di tutti i thread (misurato con std::clock)\n";
        report_file << "   - Parallel-Ratio = CPU-time / Wall-time (ideale = num_threads)\n";
        report_file << "   - Versione Hybrid: usa AoS per build, SoA per query (massima efficienza)\n\n";

        report_file << "═══════════════════════════════════════════════════════════════\n";
        report_file.close();
        std::cout << "📊 Report performance salvato: " << perf_report << "\n";
    } else {
        std::cerr << "⚠️  Impossibile salvare report performance\n";
    }

    std::cout << "\n╔═══════════════════════════════════════════════════════╗\n";
    std::cout << "║    STATISTICHE PERFORMANCE (" << (NUM_RUNS - WARMUP_RUNS) << " run misurate)      ║\n";
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
    std::cout << "╚═══════════════════════���═══════════════════════════════╝\n";

    std::cout << "\n📊 Query risultati (SoA)...\n";
    auto query_start = std::chrono::high_resolution_clock::now();

    auto top_wb = word_bigrams.get_top_n(20);
    auto top_wt = word_trigrams.get_top_n(20);
    auto top_cb = char_bigrams.get_top_n(20);
    auto top_ct = char_trigrams.get_top_n(20);

    auto query_end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> query_time = query_end - query_start;

    std::cout << "\n╔════════════════════════════════════════════════════╗\n";
    std::cout << "║ " << std::left << std::setw(50) << "Word Bigrams (n=2)" << " ║\n";
    std::cout << "╠═══════════════════════════════════════���════════════╣\n";
    std::cout << "║ Total unique: " << std::setw(35) << word_bigrams.total_unique() << " ║\n";
    std::cout << "║ Total count:  " << std::setw(35) << word_bigrams.total_count() << " ║\n";
    std::cout << "╚═══════════════════════════════════════════════���════╝\n\n";

    std::cout << "Top 20 most frequent:\n";
    for (size_t i = 0; i < top_wb.size(); ++i) {
        std::cout << std::setw(3) << (i + 1) << ". "
                  << std::left << std::setw(30) << ("\"" + top_wb[i].first + "\"")
                  << std::right << std::setw(10) << top_wb[i].second << " occurrences\n";
    }

    std::cout << "\n╔═══════════════════════════════════════════════���════╗\n";
    std::cout << "║ " << std::left << std::setw(50) << "Word Trigrams (n=3)" << " ║\n";
    std::cout << "╠═══════════════════════���════════════════════════════╣\n";
    std::cout << "║ Total unique: " << std::setw(35) << word_trigrams.total_unique() << " ║\n";
    std::cout << "║ Total count:  " << std::setw(35) << word_trigrams.total_count() << " ║\n";
    std::cout << "╚═══════════════════════════════════════════════���════╝\n\n";

    std::cout << "Top 20 most frequent:\n";
    for (size_t i = 0; i < top_wt.size(); ++i) {
        std::cout << std::setw(3) << (i + 1) << ". "
                  << std::left << std::setw(30) << ("\"" + top_wt[i].first + "\"")
                  << std::right << std::setw(10) << top_wt[i].second << " occurrences\n";
    }

    std::cout << "\n╔════════════════��══════════════════════════════���════╗\n";
    std::cout << "║ " << std::left << std::setw(50) << "Char Bigrams (n=2)" << " ║\n";
    std::cout << "╠═══════════════════════════════════════���════════════╣\n";
    std::cout << "║ Total unique: " << std::setw(35) << char_bigrams.total_unique() << " ║\n";
    std::cout << "║ Total count:  " << std::setw(35) << char_bigrams.total_count() << " ║\n";
    std::cout << "╚═══════════════════════════════════════════════���════╝\n\n";

    std::cout << "Top 20 most frequent:\n";
    for (size_t i = 0; i < top_cb.size(); ++i) {
        std::cout << std::setw(3) << (i + 1) << ". "
                  << std::left << std::setw(30) << ("\"" + top_cb[i].first + "\"")
                  << std::right << std::setw(10) << top_cb[i].second << " occurrences\n";
    }

    std::cout << "\n╔════════════════════════════════════════════════════╗\n";
    std::cout << "║ " << std::left << std::setw(50) << "Char Trigrams (n=3)" << " ║\n";
    std::cout << "╠═══════════════════════════════════════���════════════╣\n";
    std::cout << "║ Total unique: " << std::setw(35) << char_trigrams.total_unique() << " ║\n";
    std::cout << "║ Total count:  " << std::setw(35) << char_trigrams.total_count() << " ║\n";
    std::cout << "╚═══════════════════════════════════════════════���════╝\n\n";

    std::cout << "Top 20 most frequent:\n";
    for (size_t i = 0; i < top_ct.size(); ++i) {
        std::cout << std::setw(3) << (i + 1) << ". "
                  << std::left << std::setw(30) << ("\"" + top_ct[i].first + "\"")
                  << std::right << std::setw(10) << top_ct[i].second << " occurrences\n";
    }

    std::cout << "\n╔═══════════════════════════════════════════════���═══════╗\n";
    std::cout << "║           PERFORMANCE SUMMARY (ULTRA-OPTIMIZED)       ║\n";
    std::cout << "╠═══════════════════════════════════════════════════════╣\n";
    std::cout << "║ Tempo medio (" << MEASURED_RUNS << " run): " << std::setw(22) << std::fixed
              << std::setprecision(2) << mean << "s ║\n";
    std::cout << "║ Query time (SoA):       " << std::setw(22)
              << (query_time.count() * 1000) << "ms ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════╝\n\n";

    // Salvataggio risultati (output_dir già definito sopra)
    std::cout << "💾 Salvando risultati in " << output_dir << "/\n";

    CSVSaver::save_ngrams(word_bigrams, output_dir + "/word_bigrams.csv", "Word Bigrams");
    CSVSaver::save_ngrams(word_trigrams, output_dir + "/word_trigrams.csv", "Word Trigrams");
    CSVSaver::save_ngrams(char_bigrams, output_dir + "/char_bigrams.csv", "Char Bigrams");
    CSVSaver::save_ngrams(char_trigrams, output_dir + "/char_trigrams.csv", "Char Trigrams");

    std::cout << "\n✅ Completato!\n\n";

    return 0;
}

