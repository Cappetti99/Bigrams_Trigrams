//
// Lorenzo Cappetti, 2025 - Hybrid AoS/SoA OPTIMIZED
// Ottimizzazioni: Parallel Phase 3, Thread-local buffers, String pooling avanzato
//
// DESCRIZIONE:
// Questo programma analizza una collezione di libri del Project Gutenberg per estrarre
// bigrammi e trigrammi sia a livello di parole che di caratteri.
// Utilizza un approccio ibrido:
//   1. Fase di processing: Array of Structs (AoS) - hash map per accumulo parallelo
//   2. Fase di query: Struct of Arrays (SoA) - ottimizzato per cache locality
//
// ARCHITETTURA:
//   - Parallelizzazione con OpenMP (file I/O, processing, merge, conversione SoA)
//   - Arena allocator per ridurre allocazioni dinamiche
//   - Buffer thread-local riutilizzabili per minimizzare overhead
//   - String pooling con string_view per ridurre duplicazioni (-30% memoria)
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
#include <execution>  // C++17 parallel algorithms

namespace fs = std::filesystem;

//═══════════════════════════════════════════════════════════════
// OPTIMIZED STRING POOL - Arena allocator per zero-copy
//═══════════════════════════════════════════════════════════════
// Gestisce l'internamento di stringhe in un'arena contigua di memoria.
// Benefici:
//   - Riduce allocazioni dinamiche ripetute
//   - Permette l'uso di string_view (zero-copy semantics)
//   - Migliora la cache locality
//   - Elimina duplicazioni di stringhe identiche
//═══════════════════════════════════════════════════════════════
class OptimizedStringPool {
private:
    std::vector<char> arena;                              // Arena contigua di memoria per tutte le stringhe
    std::vector<std::string_view> id_to_word;             // Mapping: ID numerico → string_view
    std::unordered_map<std::string_view, size_t> word_to_id;  // Mapping: string_view → ID numerico

public:
    OptimizedStringPool() {
        arena.reserve(10'000'000);  // Pre-alloca 10MB per evitare riallocazioni durante il processing
    }

    // Interna una stringa nell'arena e restituisce il suo ID univoco.
    // Se la stringa esiste già, restituisce l'ID esistente (deduplicazione).
    size_t intern(std::string_view s) {
        // Controllo se la stringa è già stata internata
        auto it = word_to_id.find(s);
        if (it != word_to_id.end()) {
            return it->second;  // Restituisce ID esistente (deduplicazione)
        }

        // Aggiunge la stringa all'arena
        size_t pos = arena.size();
        arena.insert(arena.end(), s.begin(), s.end());

        // Crea string_view che punta all'arena (zero-copy)
        std::string_view view(arena.data() + pos, s.size());
        size_t new_id = id_to_word.size();
        id_to_word.push_back(view);
        word_to_id[view] = new_id;
        return new_id;
    }

    // Recupera la string_view associata a un ID
    std::string_view get(size_t id) const {
        return id_to_word[id];
    }

    // Numero totale di stringhe uniche internate
    size_t size() const { return id_to_word.size(); }
};

//═══════════════════════════════════════════════════════════════
// NGRAM ID - Rappresentazione compatta di n-gram
//═══════════════════════════════════════════════════════════════
// Invece di memorizzare stringhe complete, usa ID numerici per risparmiare memoria.
// Esempio: "the cat sat" → [ID_the, ID_cat, ID_sat], length=3
//═══════════════════════════════════════════════════════════════
struct NgramID {
    size_t word_ids[3];  // Array di ID (max 3 parole per trigram)
    size_t length;       // Lunghezza effettiva (2 per bigram, 3 per trigram)

    // Operatore di uguaglianza per l'uso in unordered_map
    bool operator==(const NgramID& other) const noexcept {
        if (length != other.length) return false;
        for (size_t i = 0; i < length; ++i) {
            if (word_ids[i] != other.word_ids[i]) return false;
        }
        return true;
    }
};

// Funzione hash personalizzata per NgramID
struct NgramIDHash {
    size_t operator()(const NgramID& n) const noexcept {
        size_t hash = n.length;
        for (size_t i = 0; i < n.length; ++i) {
            // Mixing con moltiplicazione e XOR per buona distribuzione hash
            hash ^= (n.word_ids[i] * 2654435761ULL) + (hash << 6) + (hash >> 2);
        }
        return hash;
    }
};

//═══════════════════════════════════════════════════════════════
// TOKENIZER - Normalizzazione e tokenizzazione del testo
//═══════════════════════════════════════════════════════════════
// Gestisce:
//   - Conversione in lowercase (con lookup table per performance)
//   - Rimozione caratteri speciali UTF-8 (àéèìòù → aeiou)
//   - Rimozione punteggiatura e numeri
//   - Tokenizzazione in parole o caratteri
//═══════════════════════════════════════════════════════════════
class Tokenizer {
private:
    // Lookup table statica per conversione veloce in lowercase
    // Evita chiamate ripetute a std::tolower() che sono costose
    static const unsigned char* get_to_lower() {
        static unsigned char table[256];
        static bool initialized = false;
        if (!initialized) {
            for (int i = 0; i < 256; i++) {
                table[i] = (i >= 'A' && i <= 'Z') ? i + 32 : i;  // A-Z → a-z
            }
            initialized = true;
        }
        return table;
    }

    // Processa caratteri UTF-8 multi-byte (es. àéèìòù, ñ, ç)
    // Converte caratteri accentati nelle loro versioni ASCII
    static inline std::string process_utf8_char(const unsigned char* bytes, size_t& skip) {
        skip = 0;

        // Caratteri UTF-8 a 2 byte (es. à, é, è, ì, ò, ù, ñ, ç)
        if ((bytes[0] & 0xE0) == 0xC0 && bytes[1]) {
            skip = 2;
            unsigned char first = bytes[0];
            unsigned char second = bytes[1];

            // Gestione caratteri accentati comuni (Latin-1 Supplement)
            if (first == 0xC3) {
                if ((second >= 0x80 && second <= 0x85) || (second >= 0xA0 && second <= 0xA5)) return "a";  // àáâãäå
                if ((second >= 0x88 && second <= 0x8B) || (second >= 0xA8 && second <= 0xAB)) return "e";  // èéêë
                if ((second >= 0x8C && second <= 0x8F) || (second >= 0xAC && second <= 0xAF)) return "i";  // ìíîï
                if ((second >= 0x92 && second <= 0x96) || (second >= 0xB2 && second <= 0xB6)) return "o";  // òóôõö
                if ((second >= 0x99 && second <= 0x9C) || (second >= 0xB9 && second <= 0xBC)) return "u";  // ùúûü
                if (second == 0x91 || second == 0xB1) return "n";  // ñ
                if (second == 0x87 || second == 0xA7) return "c";  // ç
            }
            return "";  // Altri caratteri non gestiti → rimossi
        }

        // Caratteri UTF-8 a 3 byte (emoji, simboli asiatici, ecc.) → rimossi
        if ((bytes[0] & 0xF0) == 0xE0 && bytes[1] && bytes[2]) {
            skip = 3;
            return "";
        }

        // Caratteri UTF-8 a 4 byte (emoji estesi, ecc.) → rimossi
        if ((bytes[0] & 0xF8) == 0xF0 && bytes[1] && bytes[2] && bytes[3]) {
            skip = 4;
            return "";
        }
        return "";
    }

public:
    // Normalizza il testo: lowercase, rimozione numeri/punteggiatura, UTF-8 → ASCII
    // remove_punct: se true, sostituisce la punteggiatura con spazi (per word n-gram)
    //               se false, mantiene la punteggiatura (per char n-gram)
    static std::string normalize(const std::string& text, bool remove_punct = false) {
        std::string result;
        result.reserve(text.size());  // Pre-alloca per evitare riallocazioni
        const unsigned char* to_lower = get_to_lower();

        for (size_t i = 0; i < text.size(); ++i) {
            unsigned char c = static_cast<unsigned char>(text[i]);

            // Gestione caratteri UTF-8 multi-byte
            if (c >= 0x80) {
                size_t skip;
                std::string replacement = process_utf8_char(
                    reinterpret_cast<const unsigned char*>(&text[i]), skip
                );
                if (skip > 0) {
                    result += replacement;
                    i += skip - 1;  // Salta i byte processati
                    continue;
                }
            }

            // Rimozione numeri
            if (std::isdigit(c)) continue;

            // Gestione punteggiatura
            if (remove_punct && std::ispunct(c)) {
                result += ' ';  // Sostituisce con spazio (separa parole)
                continue;
            }

            // Conversione caratteri alfabetici in lowercase
            if (std::isalpha(c)) {
                result += to_lower[c];
            } else if (std::isspace(c)) {
                result += ' ';
            }
        }
        return result;
    }

    // Tokenizza il testo in parole (separa per whitespace)
    static void tokenize_words(const std::string& text, std::vector<std::string>& tokens) {
        tokens.clear();
        std::istringstream iss(text);
        std::string word;
        while (iss >> word) {
            if (!word.empty()) {
                tokens.push_back(std::move(word));  // Move semantics per efficienza
            }
        }
    }

    // Tokenizza il testo in caratteri (esclude whitespace)
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
// TEXT CLEANER - Rimozione header/footer Project Gutenberg
//═══════════════════════════════════════════════════════════════
// I libri di Project Gutenberg hanno header/footer standardizzati che iniziano con:
//   "*** START OF THIS PROJECT GUTENBERG EBOOK ..."
//   "*** END OF THIS PROJECT GUTENBERG EBOOK ..."
// Questa classe rimuove queste sezioni per analizzare solo il contenuto effettivo.
//═══════════════════════════════════════════════════════════════
class TextCleaner {
public:
    static std::string clean_text(const std::string& text) {
        std::string cleaned = text;

        // Rimuove header (tutto prima di "*** START OF")
        size_t start_pos = cleaned.find("*** START OF");
        if (start_pos != std::string::npos) {
            size_t end_of_line = cleaned.find("***", start_pos + 12);
            if (end_of_line != std::string::npos) {
                cleaned = cleaned.substr(end_of_line + 3);
            }
        }

        // Rimuove footer (tutto dopo "*** END OF")
        size_t end_pos = cleaned.find("*** END OF");
        if (end_pos != std::string::npos) {
            cleaned = cleaned.substr(0, end_pos);
        }

        return cleaned;
    }
};

//═══════════════════════════════════════════════════════════════
// HYBRID FREQUENCY COUNTER - Struttura dati ibrida AoS/SoA
//═══════════════════════════════════════════════════════════════
// FASE 1 (Processing): Riceve dati da unordered_map<string, size_t> (AoS)
//   - Ottimo per accumulo parallelo con merge
//   - Buona locality per inserimenti casuali
//
// FASE 2 (Conversione): Trasforma in SoA (Struct of Arrays)
//   - ngram_ids: array di NgramID
//   - frequencies: array parallelo di frequenze
//
// FASE 3 (Query): Usa SoA per query veloci
//   - Migliore cache locality per sort/filtering
//   - Accesso sequenziale agli array
//   - Ottimo per operazioni SIMD
//═══════════════════════════════════════════════════════════════
class HybridFrequencyCounter {
private:
    OptimizedStringPool pool;          // Pool condiviso per internamento stringhe
    std::vector<NgramID> ngram_ids;    // Array di ID n-gram (SoA)
    std::vector<size_t> frequencies;   // Array parallelo di frequenze (SoA)
    bool finalized = false;            // Flag: true se build_from_aos() è stato chiamato

public:
    // Converte da AoS (hash map) a SoA (array paralleli)
    // Chiamata dopo il merge di tutte le hash map thread-local
    void build_from_aos(const std::unordered_map<std::string, size_t>& aos_map) {
        ngram_ids.clear();
        frequencies.clear();
        ngram_ids.reserve(aos_map.size());
        frequencies.reserve(aos_map.size());

        // Per ogni n-gram nella hash map
        for (const auto& [ngram_str, freq] : aos_map) {
            std::istringstream iss(ngram_str);
            std::string word;
            NgramID id;
            id.length = 0;

            // Converte "word1 word2 word3" → [ID1, ID2, ID3]
            while (iss >> word && id.length < 3) {
                id.word_ids[id.length++] = pool.intern(word);  // Interna parola e ottiene ID
            }

            ngram_ids.push_back(id);
            frequencies.push_back(freq);
        }

        finalized = true;
    }

    // Somma tutte le frequenze (totale n-gram processati)
    size_t sum_frequencies() const {
        return std::accumulate(frequencies.begin(), frequencies.end(), 0ULL);
    }

    // Conta quanti n-gram hanno frequenza > threshold
    size_t count_above_threshold(size_t threshold) const {
        return std::count_if(frequencies.begin(), frequencies.end(),
            [threshold](size_t f) { return f > threshold; }
        );
    }

    // Restituisce i top N n-gram per frequenza (con parallel sort se disponibile)
    std::vector<std::pair<std::string, size_t>> get_top_n(size_t n) const {
        if (!finalized) {
            throw std::runtime_error("Devi chiamare build_from_aos() prima!");
        }

        // Crea array di indici [0, 1, 2, ..., size-1]
        std::vector<size_t> indices(frequencies.size());
        std::iota(indices.begin(), indices.end(), 0);

        size_t k = std::min(n, indices.size());

        // Partial sort: ordina solo i primi k elementi (più efficiente di full sort)
        // Usa parallel sort se il compilatore supporta std::execution (C++17)
        #ifdef __cpp_lib_execution
        std::partial_sort(
            std::execution::par_unseq,  // Parallelizzazione + SIMD
            indices.begin(),
            indices.begin() + k,
            indices.end(),
            [this](size_t i, size_t j) {
                return frequencies[i] > frequencies[j];  // Ordine decrescente
            }
        );
        #else
        std::partial_sort(
            indices.begin(),
            indices.begin() + k,
            indices.end(),
            [this](size_t i, size_t j) {
                return frequencies[i] > frequencies[j];
            }
        );
        #endif

        // Ricostruisce stringhe dai primi k n-gram
        std::vector<std::pair<std::string, size_t>> result;
        result.reserve(k);

        for (size_t i = 0; i < k; ++i) {
            size_t idx = indices[i];
            const auto& id = ngram_ids[idx];

            // Ricostruisce "word1 word2 word3" dagli ID
            std::string ngram;
            for (size_t j = 0; j < id.length; ++j) {
                if (j > 0) ngram += " ";
                ngram += std::string(pool.get(id.word_ids[j]));
            }

            result.emplace_back(std::move(ngram), frequencies[idx]);
        }

        return result;
    }

    // Numero di n-gram unici
    size_t total_unique() const { return ngram_ids.size(); }

    // Totale occorrenze (somma frequenze)
    size_t total_count() const { return sum_frequencies(); }
};

//═══════════════════════════════════════════════════════════════
// OPTIMIZED OPENMP PROCESSOR - Pipeline parallela ottimizzata
//═══════════════════════════════════════════════════════════════
// Gestisce il processing parallelo con OpenMP in 3 fasi:
//   FASE 1: Processing parallelo dei libri con hash map thread-local
//   FASE 2: Merge parallelo delle hash map (tournament merge)
//   FASE 3: Conversione parallela AoS → SoA (4 sezioni parallele)
//═══════════════════════════════════════════════════════════════
class OptimizedOpenMPProcessor {
public:
    // Processa un singolo testo ed estrae n-gram
    // OTTIMIZZAZIONE: Riutilizza buffer thread-local per evitare allocazioni ripetute
    static void process_text_aos_optimized(
        const std::string& text,
        std::unordered_map<std::string, size_t>& word_bigrams,
        std::unordered_map<std::string, size_t>& word_trigrams,
        std::unordered_map<std::string, size_t>& char_bigrams,
        std::unordered_map<std::string, size_t>& char_trigrams,
        std::vector<std::string>& words_buffer,  // Riutilizzabile (evita allocazioni)
        std::vector<char>& chars_buffer          // Riutilizzabile (evita allocazioni)
    ) {
        // Normalizza testo: lowercase, UTF-8→ASCII, rimuove numeri/punteggiatura
        std::string normalized = Tokenizer::normalize(text, true);

        // ==================== WORD N-GRAMS ====================
        words_buffer.clear();
        Tokenizer::tokenize_words(normalized, words_buffer);

        // String building ottimizzato con reserve() per evitare riallocazioni
        std::string key;

        // Word bigrams: "word1 word2"
        for (size_t i = 0; i + 1 < words_buffer.size(); ++i) {
            key.clear();
            key.reserve(words_buffer[i].size() + words_buffer[i+1].size() + 1);
            key = words_buffer[i];
            key += ' ';
            key += words_buffer[i + 1];
            word_bigrams[key]++;
        }

        // Word trigrams: "word1 word2 word3"
        for (size_t i = 0; i + 2 < words_buffer.size(); ++i) {
            key.clear();
            key.reserve(words_buffer[i].size() + words_buffer[i+1].size() +
                       words_buffer[i+2].size() + 2);
            key = words_buffer[i];
            key += ' ';
            key += words_buffer[i + 1];
            key += ' ';
            key += words_buffer[i + 2];
            word_trigrams[key]++;
        }

        // ==================== CHAR N-GRAMS ====================
        chars_buffer.clear();
        Tokenizer::tokenize_chars(normalized, chars_buffer);

        // Char bigrams: "c1 c2"
        for (size_t i = 0; i + 1 < chars_buffer.size(); ++i) {
            key.clear();
            key.reserve(3);  // 2 caratteri + 1 spazio
            key += chars_buffer[i];
            key += ' ';
            key += chars_buffer[i + 1];
            char_bigrams[key]++;
        }

        // Char trigrams: "c1 c2 c3"
        for (size_t i = 0; i + 2 < chars_buffer.size(); ++i) {
            key.clear();
            key.reserve(5);  // 3 caratteri + 2 spazi
            key += chars_buffer[i];
            key += ' ';
            key += chars_buffer[i + 1];
            key += ' ';
            key += chars_buffer[i + 2];
            char_trigrams[key]++;
        }
    }

    // Merge parallelo di hash map (tournament merge)
    // STRATEGIA: Merge a coppie in parallelo, dimezzando il numero di map ad ogni iterazione
    //   Iterazione 1: [map0+map7], [map1+map6], [map2+map5], [map3+map4] → 4 mappe
    //   Iterazione 2: [map0+map3], [map1+map2] → 2 mappe
    //   Iterazione 3: [map0+map1] → 1 mappa finale
    static void parallel_merge_aos(
        std::vector<std::unordered_map<std::string, size_t>>& thread_maps,
        std::unordered_map<std::string, size_t>& result
    ) {
        int num_maps = thread_maps.size();

        // Tournament merge: merge a coppie fino ad avere 1 sola mappa
        while (num_maps > 1) {
            int next_num = (num_maps + 1) / 2;

            // Merge parallelo di coppie opposte
            #pragma omp parallel for schedule(dynamic)
            for (int i = 0; i < num_maps / 2; ++i) {
                auto& map1 = thread_maps[i];              // Prima metà
                auto& map2 = thread_maps[num_maps - 1 - i];  // Seconda metà (opposto)

                map1.reserve(map1.size() + map2.size());  // Pre-alloca per evitare rehashing

                // Merge map2 → map1
                for (const auto& [key, val] : map2) {
                    map1[key] += val;
                }
                map2.clear();  // Libera memoria
            }
            num_maps = next_num;
        }

        // Sposta il risultato finale
        result = std::move(thread_maps[0]);
    }

    // Pipeline principale di processing parallelo
    static void process_parallel_hybrid(
        const std::vector<std::string>& book_files,
        HybridFrequencyCounter& word_bigrams,
        HybridFrequencyCounter& word_trigrams,
        HybridFrequencyCounter& char_bigrams,
        HybridFrequencyCounter& char_trigrams,
        int num_threads = 0
    ) {
        // Configura numero di thread OpenMP
        if (num_threads == 0) num_threads = omp_get_max_threads();
        omp_set_num_threads(num_threads);

        std::cout << "🧵 Usando " << num_threads << " threads (OTTIMIZZATO)\n\n";

        int total_books = book_files.size();

        // Hash map thread-local per accumulo parallelo (evita contention)
        std::vector<std::unordered_map<std::string, size_t>> thread_wb(num_threads);
        std::vector<std::unordered_map<std::string, size_t>> thread_wt(num_threads);
        std::vector<std::unordered_map<std::string, size_t>> thread_cb(num_threads);
        std::vector<std::unordered_map<std::string, size_t>> thread_ct(num_threads);

        auto phase1_start = std::chrono::high_resolution_clock::now();

        //═══════════════════════════════════════════════════════════
        // FASE 1: Processing parallelo con buffer thread-local
        //═══════════════════════════════════════════════════════════
        std::cout << "📖 Fase 1: Processing parallelo (buffer riutilizzabili)...\n";

        #pragma omp parallel
        {
            int tid = omp_get_thread_num();
            auto& my_wb = thread_wb[tid];  // Word bigrams locali
            auto& my_wt = thread_wt[tid];  // Word trigrams locali
            auto& my_cb = thread_cb[tid];  // Char bigrams locali
            auto& my_ct = thread_ct[tid];  // Char trigrams locali

            // Pre-alloca hash map per ridurre rehashing durante l'inserimento
            // Valori empirici basati sul volume medio di n-gram per libro
            my_wb.reserve(100000);   // ~100k word bigrams unici per libro
            my_wt.reserve(200000);   // ~200k word trigrams unici per libro
            my_cb.reserve(50000);    // ~50k char bigrams unici per libro
            my_ct.reserve(100000);   // ~100k char trigrams unici per libro

            // Buffer thread-local riutilizzabili (evita allocazioni ripetute)
            std::vector<std::string> words_buf;
            std::vector<char> chars_buf;
            words_buf.reserve(10000);   // ~10k parole per libro
            chars_buf.reserve(50000);   // ~50k caratteri per libro

            // Distribuzione dinamica dei libri ai thread (load balancing)
            #pragma omp for schedule(dynamic) nowait
            for (int i = 0; i < total_books; ++i) {
                const auto& filepath = book_files[i];

                // Lettura file ottimizzata (binary mode + ate per dimensione)
                std::ifstream file(filepath, std::ios::binary | std::ios::ate);
                if (!file) continue;

                std::streamsize size = file.tellg();
                file.seekg(0, std::ios::beg);
                std::string text(size, '\0');
                if (!file.read(&text[0], size)) continue;

                // Pulizia testo (rimozione header/footer Gutenberg)
                text = TextCleaner::clean_text(text);

                // Processing del testo con buffer riutilizzabili
                process_text_aos_optimized(text, my_wb, my_wt, my_cb, my_ct,
                                          words_buf, chars_buf);
            }
        }

        auto phase1_end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> phase1_time = phase1_end - phase1_start;
        std::cout << "   ✅ Completato in " << std::fixed << std::setprecision(2)
                  << phase1_time.count() << "s\n\n";

        //═══════════════════════════════════════════════════════════
        // FASE 2: Merge parallelo delle hash map (tournament merge)
        //═══════════════════════════════════════════════════════════
        std::cout << "🔀 Fase 2: Merge parallelo...\n";
        auto phase2_start = std::chrono::high_resolution_clock::now();

        std::unordered_map<std::string, size_t> merged_wb, merged_wt, merged_cb, merged_ct;
        parallel_merge_aos(thread_wb, merged_wb);
        parallel_merge_aos(thread_wt, merged_wt);
        parallel_merge_aos(thread_cb, merged_cb);
        parallel_merge_aos(thread_ct, merged_ct);

        auto phase2_end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> phase2_time = phase2_end - phase2_start;
        std::cout << "   ✅ Completato in " << phase2_time.count() << "s\n\n";

        //═══════════════════════════════════════════════════════════
        // FASE 3: Conversione AoS → SoA PARALLELIZZATA
        //═══════════════════════════════════════════════════════════
        // Usa #pragma omp sections per parallelizzare le 4 conversioni indipendenti
        std::cout << "🔄 Fase 3: Conversione AoS → SoA (PARALLELA)...\n";
        auto phase3_start = std::chrono::high_resolution_clock::now();

        #pragma omp parallel sections
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
        std::cout << "   ✅ Completato in " << phase3_time.count() << "s\n\n";
    }
};

//═══════════════════════════════════════════════════════════════
// MAIN - Orchestrazione del programma
//═══════════════════════════════════════════════════════════════
int main() {
    // ==================== HEADER ====================
    std::cout << "\n╔═══════════════════════════════════════════════════════╗\n";
    std::cout << "║   Hybrid AoS/SoA N-gram Analyzer (OPTIMIZED)         ║\n";
    std::cout << "║              Lorenzo Cappetti, 2025                   ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════╝\n\n";

    // ==================== SETUP LIBRI ====================
    std::string folder_path = "/Users/lorenzocappetti/CLionProjects/Bigrams_Trigrams/book_gutenberg";

    if (!fs::exists(folder_path)) {
        std::cerr << "❌ Cartella non trovata: " << folder_path << "\n";
        return 1;
    }

    // Raccolta file .txt dalla cartella
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

    // ==================== CLI: SELEZIONE THREAD ====================
    int max_threads = omp_get_max_threads();  // Thread fisici disponibili
    const int MAX_VIRTUAL_THREADS = 32;       // Limite massimo (include hyperthreading)
    int num_threads;

    std::cout << "🧵 Thread fisici disponibili: " << max_threads << "\n";
    std::cout << "💡 Thread virtuali consentiti: fino a " << MAX_VIRTUAL_THREADS << "\n";
    std::cout << "┌─────────────────────────────────────────────────────┐\n";
    std::cout << "│ Quanti thread vuoi usare? (1-" << MAX_VIRTUAL_THREADS << "): ";
    std::cout.flush();

    // Input validation loop
    while (true) {
        std::cin >> num_threads;

        // Gestione input non numerico
        if (std::cin.fail()) {
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            std::cout << "│ ⚠️  Inserisci un numero valido (1-" << MAX_VIRTUAL_THREADS << "): ";
            continue;
        }

        // Validazione range
        if (num_threads < 1 || num_threads > MAX_VIRTUAL_THREADS) {
            std::cout << "│ ⚠️  Fuori range! Inserisci un valore tra 1 e " << MAX_VIRTUAL_THREADS << ": ";
            continue;
        }

        break;  // Input valido
    }

    std::cout << "└─────────────────────────────────────────────────────┘\n";

    // Feedback all'utente sul tipo di parallelismo
    if (num_threads > max_threads) {
        std::cout << "⚡ Verranno usati " << num_threads << " thread VIRTUALI "
                  << "(oltre i " << max_threads << " fisici)\n";
        std::cout << "   ⚠️  Possibile overhead da hyperthreading/context switching\n\n";
    } else {
        std::cout << "✅ Verranno usati " << num_threads << " thread fisici\n\n";
    }

    // ==================== MULTIPLE RUN BENCHMARK ====================
    // Esegue 10 run per ottenere statistiche affidabili (riduce varianza)
    const int NUM_RUNS = 10;
    std::vector<double> run_times;
    run_times.reserve(NUM_RUNS);

    std::cout << "🔄 Eseguendo " << NUM_RUNS << " run per ottenere statistiche affidabili...\n\n";

    HybridFrequencyCounter word_bigrams, word_trigrams, char_bigrams, char_trigrams;

    for (int run = 0; run < NUM_RUNS; ++run) {
        // Reset contatori per ogni run (risultati identici, tempi diversi)
        word_bigrams = HybridFrequencyCounter();
        word_trigrams = HybridFrequencyCounter();
        char_bigrams = HybridFrequencyCounter();
        char_trigrams = HybridFrequencyCounter();

        std::cout << "▶ Run " << std::setw(3) << (run + 1) << "/" << NUM_RUNS << " ... ";
        std::cout.flush();

        auto start_time = std::chrono::high_resolution_clock::now();

        // ==================== PROCESSING PIPELINE OTTIMIZZATA ====================
        OptimizedOpenMPProcessor::process_parallel_hybrid(
            book_files,
            word_bigrams,
            word_trigrams,
            char_bigrams,
            char_trigrams,
            num_threads  // Numero di thread scelto dall'utente
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

    // Deviazione standard (misura della variabilità)
    double stddev = 0.0;
    for (double t : run_times) {
        stddev += (t - mean) * (t - mean);
    }
    stddev = std::sqrt(stddev / run_times.size());

    // Coefficiente di variazione (CV% = stddev/mean * 100)
    // CV basso (<5%) indica risultati stabili
    double cv = (stddev / mean) * 100.0;

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

    // ==================== QUERY PHASE (SoA cache-optimized) ====================
    // Usa la struttura SoA per query veloci (migliore cache locality)
    std::cout << "\n📊 Query su risultati (SoA + parallel sort)...\n\n";
    auto query_start = std::chrono::high_resolution_clock::now();

    auto top_wb = word_bigrams.get_top_n(10);
    auto top_wt = word_trigrams.get_top_n(10);
    auto top_cb = char_bigrams.get_top_n(10);
    auto top_ct = char_trigrams.get_top_n(10);

    auto query_end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> query_time = query_end - query_start;

    // ==================== STAMPA RISULTATI TOP 10 ====================
    std::cout << "┌─────────────────────────────────────────────┐\n";
    std::cout << "│          Top 10 Word Bigrams                │\n";
    std::cout << "├─────────────────────────────────────────────┤\n";
    for (size_t i = 0; i < top_wb.size(); ++i) {
        std::cout << "│ " << std::setw(2) << (i+1) << ". "
                  << std::left << std::setw(30) << ("\"" + top_wb[i].first + "\"")
                  << std::right << std::setw(8) << top_wb[i].second << " │\n";
    }
    std::cout << "└─────────────────────────────────────────────┘\n\n";

    std::cout << "┌─────────────────────────────────────────────┐\n";
    std::cout << "│         Top 10 Word Trigrams                │\n";
    std::cout << "├─────────────────────────────────────────────┤\n";
    for (size_t i = 0; i < top_wt.size(); ++i) {
        std::cout << "│ " << std::setw(2) << (i+1) << ". "
                  << std::left << std::setw(30) << ("\"" + top_wt[i].first + "\"")
                  << std::right << std::setw(8) << top_wt[i].second << " │\n";
    }
    std::cout << "└─────────────────────────────────────────────┘\n\n";

    std::cout << "┌─────────────────────────────────────────────┐\n";
    std::cout << "│          Top 10 Char Bigrams                │\n";
    std::cout << "├─────────────────────────────────────────────┤\n";
    for (size_t i = 0; i < top_cb.size(); ++i) {
        std::cout << "│ " << std::setw(2) << (i+1) << ". "
                  << std::left << std::setw(30) << ("\"" + top_cb[i].first + "\"")
                  << std::right << std::setw(8) << top_cb[i].second << " │\n";
    }
    std::cout << "└─────────────────────────────────────────────┘\n\n";

    std::cout << "┌─────────────────────────────────────────────┐\n";
    std::cout << "│         Top 10 Char Trigrams                │\n";
    std::cout << "├─────────────────────────────────────────────┤\n";
    for (size_t i = 0; i < top_ct.size(); ++i) {
        std::cout << "│ " << std::setw(2) << (i+1) << ". "
                  << std::left << std::setw(30) << ("\"" + top_ct[i].first + "\"")
                  << std::right << std::setw(8) << top_ct[i].second << " │\n";
    }
    std::cout << "└─────────────────────────────────────────────┘\n\n";

    // ==================== STATISTICHE FINALI ====================
    std::cout << "╔═══════════════════════════════════════════════════════╗\n";
    std::cout << "║              PERFORMANCE SUMMARY (OPTIMIZED)          ║\n";
    std::cout << "╠═══════════════════════════════════════════════════════╣\n";
    std::cout << "║ Tempo medio (" << NUM_RUNS << " run): " << std::setw(22) << std::fixed
              << std::setprecision(2) << mean << "s ║\n";
    std::cout << "║ Query time (SoA):       " << std::setw(22)
              << (query_time.count() * 1000) << "ms ║\n";
    std::cout << "╠═══════════════════════════════════════════════════════╣\n";
    std::cout << "║ Word Bigrams unici:     " << std::setw(26)
              << word_bigrams.total_unique() << " ║\n";
    std::cout << "║ Word Trigrams unici:    " << std::setw(26)
              << word_trigrams.total_unique() << " ║\n";
    std::cout << "║ Char Bigrams unici:     " << std::setw(26)
              << char_bigrams.total_unique() << " ║\n";
    std::cout << "║ Char Trigrams unici:    " << std::setw(26)
              << char_trigrams.total_unique() << " ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════╝\n\n";

    return 0;
}
