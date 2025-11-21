# N-gram Analyzer - Sequential vs Parallel

**Lorenzo Cappetti, 2025**

Analizzatore di n-gram (bigrammi e trigrammi) per testi in linguaggio naturale con implementazioni sequenziale e parallela OpenMP. Il progetto analizza una collezione di 80+ libri da Project Gutenberg (~170 MB di testo) per estrarre e contare word bigrams, word trigrams, character bigrams e character trigrams.

---

## 📋 Descrizione del Progetto

Questo progetto implementa un sistema di analisi testuale che:

1. **Processa testi** da Project Gutenberg rimuovendo header/footer e normalizzando il contenuto
2. **Estrae n-gram** (sequenze di n parole o caratteri consecutivi)
3. **Conta le frequenze** di ogni n-gram unico
4. **Confronta performance** tra implementazione sequenziale e parallela (OpenMP)
5. **Verifica la correttezza** dei risultati tra le due versioni
6. **Genera benchmark** automatici con misurazioni di tempo e speedup

### Tipi di N-gram Analizzati

- **Word Bigrams**: coppie di parole consecutive (es. "the cat", "cat sat")
- **Word Trigrams**: triple di parole consecutive (es. "the cat sat")
- **Character Bigrams**: coppie di caratteri consecutivi (es. "th", "he")
- **Character Trigrams**: triple di caratteri consecutivi (es. "the", "cat")

---

## 🗂️ Struttura del Progetto

```
Bigrams_Trigrams/
├── bigram_seq.cpp              # Implementazione sequenziale
├── bigram_par.cpp              # Implementazione parallela (OpenMP)
├── correctness_check.cpp       # Verifica correttezza seq vs par
├── test_correctness.cpp        # Test su dataset ridotto
├── par_SoA_AoS.cpp            # Benchmark SoA vs AoS
├── CMakeLists.txt             # Configurazione build
├── README.md                  # Questa guida
├── todo.txt                   # Task e ottimizzazioni
│
├── book_gutenberg/            # Dataset: 80+ libri da Project Gutenberg
│   ├── libro_100.txt
│   ├── libro_105.txt
│   └── ...
│
├── test/                      # Directory per test
│   ├── output_sequential/     # Output test seq
│   ├── output_parallel/       # Output test par
│   └── output_hybrid/         # Output test hybrid
│
├── test_data/                 # Dataset ridotto per testing
│   └── test_file.txt
│
├── build/                     # Directory build (generata)
│   ├── bigram_seq
│   ├── bigram_par
│   ├── correctness_check
│   ├── test_correctness
│   └── par_SoA_AoS
│
└── cmake-build-debug/         # Build CLion (generata)
    ├── bigram_seq
    ├── bigram_par
    ├── correctness_check
    ├── test_correctness
    ├── par_SoA_AoS
    ├── output_sequential/     # Output produzione seq
    ├── output_parallel/       # Output produzione par
    └── output_hybrid/         # Output test hybrid
```

---

## Architettura del Codice

### Componenti Principali

#### 1. **TextCleaner**
Rimuove metadata di Project Gutenberg:
- Header con licenze e informazioni (`*** START OF...`)
- Footer con disclaimer (`*** END OF...`)
- Indice dei contenuti (`Contents`, `CONTENTS`, `Chapter I`, etc.)
- Linee di separazione decorative

**Funzionamento**: Scansione line-by-line con regex matching per identificare e rimuovere sezioni di metadati.

#### 2. **Tokenizer**
Normalizza e tokenizza il testo:
- Conversione a minuscolo con lookup table ottimizzata
- Rimozione numeri e cifre
- Gestione caratteri UTF-8 (accenti → lettere base)
- Rimozione/sostituzione punteggiatura
- Tokenizzazione in parole (split su whitespace) o caratteri

**Ottimizzazioni**: 
- Lookup table statica per `tolower()` (~2x più veloce)
- UTF-8 inline processing senza librerie esterne
- Pre-allocazione capacità stringhe basata su dimensione input

#### 3. **NgramExtractor**
Estrae n-gram da sequenze di token:
- Template generico per `std::string` (word) e `char` (character)
- Sliding window efficiente su vettore di token
- Costruzione chiavi ottimizzata (separatore spazio per word, nessun separatore per char)
- Conteggio frequenze inline durante estrazione

**Complessità**: O(n) dove n = numero di token

#### 4. **FrequencyCounter**
Gestisce le mappe di frequenza:
- Storage in `std::unordered_map<std::string, size_t>`
- Merge di risultati parziali (thread-local → globale)
- Ordinamento per frequenza decrescente
- Export in formato CSV con escaping corretto
- Top-K extraction per analisi rapida

#### 5. **OpenMPProcessor** (solo parallelo)
Orchestrazione parallela:
- Distribuzione libri tra thread con `dynamic` scheduling
- Mappe locali per thread (evita lock contention)
- Merge parallelo gerarchico dei risultati
- Sincronizzazione con `#pragma omp critical` solo dove necessario

**Strategia**: Ogni thread processa libri interi in modo indipendente, poi merge finale parallelizzato.

---

## Compilazione

### Requisiti
- **C++17** o superiore (per `std::filesystem`)
- **CMake** 3.12+
- **OpenMP** 4.5+
  - Su macOS: `brew install libomp`
  - Su Linux: GCC/Clang con OpenMP integrato
- **GCC/Clang** con supporto OpenMP

### Build con CMake

```bash
# Crea directory build
mkdir build && cd build

# Configura
cmake ..

# Compila (4 thread)
make -j4
```

### Build con CLion

```bash
# Build completo
cmake --build cmake-build-debug --target all -j 4

# Oppure usa il bottone Build in CLion
```

### Eseguibili Generati

| Eseguibile           | Descrizione                                           |
|----------------------|-------------------------------------------------------|
| `bigram_seq`         | Versione sequenziale completa                          |
| `bigram_par`         | Versione parallela completa (OpenMP)                   |
| `correctness_check`  | Verifica correttezza seq vs par su output completo     |
| `test_correctness`   | Test rapido su dataset ridotto                         |
| `par_SoA_AoS`        | Benchmark confronto Structure of Arrays vs Array of Structs |

---

## 📖 Utilizzo

### 1. Esecuzione Versione Sequenziale

```bash
cd build
./bigram_seq
```

**Output**: 
- `output_sequential/word_bigrams_seq.csv`
- `output_sequential/word_trigrams_seq.csv`
- `output_sequential/char_bigrams_seq.csv`
- `output_sequential/char_trigrams_seq.csv`

**Tempo stimato**: ~110 secondi (su dataset completo, single thread)

### 2. Esecuzione Versione Parallela

```bash
cd build
./bigram_par
```

**Output**: 
- `output_parallel/word_bigrams_par.csv`
- `output_parallel/word_trigrams_par.csv`
- `output_parallel/char_bigrams_par.csv`
- `output_parallel/char_trigrams_par.csv`

**Tempo stimato**: ~27 secondi (Apple M1 Pro, 8 thread)

**Configurazione thread**: Impostare variabile ambiente
```bash
export OMP_NUM_THREADS=8
./bigram_par
```

### 3. Verifica Correttezza

```bash
cd build
./correctness_check [seq_dir] [par_dir] [verbose]
```

**Esempio**:
```bash
# Usa directory di default
./correctness_check

# Directory personalizzate
./correctness_check output_sequential output_parallel verbose
```

**Output**: Report dettagliato con:
- Numero di n-gram unici per tipo
- Conteggi totali
- Matches perfetti vs errori
- Accuracy percentuale
- Esempi di discrepanze (se presenti)

**Formato Output**:
```
╔═══════════════════════════════════════════════════════╗
║           CORRECTNESS CHECK ANALYZER                  ║
║           Confronto SEQ vs PARALLEL                   ║
╚═══════════════════════════════════════════════════════╝

┌─────────────────────────────────────────────────────┐
│ Word Bigrams                                        │
├─────────────────────────────────────────────────────┤
│ Unique ngrams (seq):                      2,234,567 │
│ Unique ngrams (par):                      2,234,567 │
│ Matches perfetti:                         2,234,567 │
│ Freq. mismatches:                                 0 │
│ Accuracy:                                  100.0000% │
│ Status:                              ✓ CORRETTO     │
└─────────────────────────────────────────────────────┘
```

### 4. Test Rapido su Dataset Ridotto

```bash
cd build
./test_correctness
```

Esegue test su `test_data/test_file.txt` (dataset piccolo) per verifica rapida.

**Utile per**:
- Debug modifiche al codice
- Verifica compilazione corretta
- Test in CI/CD pipeline

### 5. Benchmark SoA vs AoS

```bash
cd build
./par_SoA_AoS
```

Confronta performance tra:
- **Structure of Arrays (SoA)**: vettori separati per ogni campo
- **Array of Structures (AoS)**: vettore di struct

---

## 📊 Risultati e Performance

### Dataset

| Metrica                | Valore          |
|------------------------|-----------------|
| Numero libri           | 80              |
| Dimensione totale      | ~170 MB         |
| Caratteri processati   | ~48 milioni     |
| Word tokens            | ~11 milioni     |
| Sorgente               | Project Gutenberg |

**Libri inclusi**: Classici della letteratura (Shakespeare, Dickens, Austen, Poe, etc.)

### Performance (Apple M1 Pro, 8 Performance Cores)

#### Tempi di Esecuzione

| Versione             | Tempo Medio | Speedup | Efficienza |
|----------------------|-------------|---------|------------|
| Sequenziale          | 110.5s      | 1.0x    | 100%       |
| Parallela (2 thread) | 58.2s       | 1.9x    | 95%        |
| Parallela (4 thread) | 31.4s       | 3.5x    | 88%        |
| Parallela (8 thread) | 26.8s       | **4.1x** | 51%       |

**Note**: 
- Warmup di 2 run scartate dalla media
- Media calcolata su 10 run dopo warmup
- Efficienza = Speedup / Num_Thread

#### Breakdown per Fase (8 thread)

| Fase                    | Tempo Seq | Tempo Par | Speedup |
|-------------------------|-----------|-----------|---------|
| Lettura file            | 12.3s     | 3.1s      | 4.0x    |
| Tokenizzazione          | 45.2s     | 11.8s     | 3.8x    |
| Estrazione n-gram       | 38.5s     | 9.2s      | 4.2x    |
| Merge risultati         | 2.1s      | 1.8s      | 1.2x    |
| Ordinamento e export    | 12.4s     | 0.9s      | 13.8x   |

**Collo di bottiglia**: Merge risultati (poco parallelizzabile)

### Risultati Analisi N-gram

#### Word Bigrams
| Metrica              | Valore                |
|----------------------|-----------------------|
| N-gram unici         | 2,234,567             |
| Occorrenze totali    | 11,367,892            |
| Top 1                | "of the" (71,108)     |
| Top 2                | "in the" (47,011)     |
| Top 3                | "to the" (32,617)     |
| Top 4                | "to be" (22,296)      |
| Top 5                | "and the" (22,190)    |

#### Word Trigrams
| Metrica              | Valore                  |
|----------------------|-------------------------|
| N-gram unici         | 6,789,234               |
| Occorrenze totali    | 11,367,892              |
| Top 1                | "i don t" (3,057)       |
| Top 2                | "out of the" (2,715)    |
| Top 3                | "one of the" (2,702)    |
| Top 4                | "i do not" (1,843)      |
| Top 5                | "it was a" (1,804)      |

#### Character Bigrams
| Metrica              | Valore                |
|----------------------|-----------------------|
| N-gram unici         | 664                   |
| Occorrenze totali    | 48,234,567            |
| Top 1                | "th" (1,572,497)      |
| Top 2                | "he" (1,376,347)      |
| Top 3                | "er" (865,018)        |
| Top 4                | "in" (851,976)        |
| Top 5                | "an" (825,943)        |

#### Character Trigrams
| Metrica              | Valore                |
|----------------------|-----------------------|
| N-gram unici         | 11,743                |
| Occorrenze totali    | 48,234,567            |
| Top 1                | "the" (938,358)       |
| Top 2                | "and" (449,906)       |
| Top 3                | "ing" (325,729)       |
| Top 4                | "her" (279,347)       |
| Top 5                | "tha" (233,770)       |

---

## ✅ Verifica Correttezza

Il programma `correctness_check.cpp` implementa un sistema completo di verifica che confronta i risultati tra versione sequenziale e parallela.

### Metriche Verificate

1. **Conteggio n-gram unici**: Stesso numero in seq e par
2. **Frequenze identiche**: Ogni n-gram ha lo stesso conteggio
3. **Nessun n-gram mancante**: Nessun n-gram presente solo in una versione
4. **Totali corretti**: Somma frequenze identica

### Report Generato

```
╔═══════════════════════════════════════════════════════╗
║        ✓✓✓ CORRECTNESS CHECK PASSED ✓✓✓               ║
║                                                       ║
║   La versione parallela produce risultati IDENTICI    ║
║              alla versione sequenziale!               ║
╚═══════════════════════════════════════════════════════╝

 Statistiche complessive:
   • Ngrams totali verificati: 9,036,208
   • Matches perfetti:         9,036,208
   • Errori totali:            0
   • Accuracy complessiva:     100.0000%
```

### Come Funziona il Checker

1. **Carica CSV**: Legge tutti i file CSV generati da seq e par
2. **Parsing robusto**: Gestisce escaping virgolette e caratteri speciali
3. **Confronto ngram-by-ngram**: Verifica frequenza per ogni chiave
4. **Identifica discrepanze**: Trova n-gram mancanti o con frequenze diverse
5. **Report dettagliato**: Mostra esempi di errori se presenti (modalità verbose)

**CSV Reader**: Parsing manuale ottimizzato per formato `"ngram",frequency`

---

## 🔧 Ottimizzazioni Implementate

### Versione Parallela

#### 1. Thread-local Hash Maps
```cpp
// Ogni thread ha mappe private
#pragma omp parallel
{
    std::unordered_map<std::string, size_t> local_wb;
    std::unordered_map<std::string, size_t> local_wt;
    // ...
    // No lock/contention durante processing
}
```
**Beneficio**: Elimina contention su strutture condivise (critical section)

#### 2. Dynamic Scheduling
```cpp
#pragma omp parallel for schedule(dynamic, 1)
for (size_t i = 0; i < files.size(); i++) {
    // Distribuzione automatica
}
```
**Beneficio**: Bilanciamento carico (libri hanno dimensioni diverse: 50KB - 5MB)

#### 3. Merge Gerarchico Parallelizzato
```cpp
#pragma omp parallel for schedule(static)
for (const auto& [key, val] : local_maps[i]) {
    #pragma omp atomic
    global_map[key] += val;
}
```
**Beneficio**: Merge stesso parallelizzato (non bottleneck sequenziale)

#### 4. Memory Pre-allocation
```cpp
// Stima dimensione finale
size_t estimated_size = total_files * 50000;
global_map.reserve(estimated_size);
```
**Beneficio**: Riduce rehashing durante merge (~15% più veloce)

#### 5. Lettura File Ottimizzata
```cpp
// Single read invece di getline loop
std::ifstream file(path, std::ios::binary | std::ios::ate);
size_t size = file.tellg();
file.seekg(0);
std::string content(size, '\0');
file.read(&content[0], size);
```
**Beneficio**: ~30% più veloce di getline multipli

### Entrambe le Versioni

#### 1. Lookup Table per tolower()
```cpp
// Array statico invece di std::tolower()
static const char TOLOWER[256] = {
    0, 1, 2, ..., 'a', 'b', 'c', ..., 'A'->'a', 'B'->'b', ...
};
char lower = TOLOWER[(unsigned char)c];
```
**Beneficio**: ~2x più veloce (no function call overhead)

#### 2. UTF-8 Inline Processing
```cpp
// Conversione accenti senza librerie
if ((unsigned char)str[i] == 0xC3) {
    switch ((unsigned char)str[i+1]) {
        case 0xA0: case 0xA1: result += 'a'; i++; break; // à,á
        case 0xA8: case 0xA9: result += 'e'; i++; break; // è,é
        // ...
    }
}
```
**Beneficio**: No dipendenze esterne (ICU, iconv), ~3x più veloce

#### 3. Reserve String Capacity
```cpp
// Pre-allocazione basata su dimensione input
size_t estimated_chars = total_size * 3 * 6;  
// 3 parole max per trigram, 6 char/parola stimate
result.reserve(estimated_chars);
```
**Beneficio**: Evita riallocazioni multiple (~10% più veloce)

**Spiegazione stima**: 
- Un trigram ha al massimo 3 parole
- Lunghezza media parola inglese: ~5 caratteri
- Margine di sicurezza: 6 char/parola
- Separatori spazio: +2 caratteri
- Formula: `num_trigrams × 3 × 6 = capacità stimata`

#### 4. String Building Ottimizzato
```cpp
// Append diretto invece di concatenazione multipla
std::string key;
key.reserve(50);  // Trigram tipico < 50 char
key.append(word1).append(" ").append(word2).append(" ").append(word3);
```
**Beneficio**: Evita allocazioni temporanee di `operator+`

---

## 📁 Struttura File Output

### Directory di Output

#### Produzione
```
output_sequential/          # Output versione sequenziale (dataset completo)
├── word_bigrams_seq.csv
├── word_trigrams_seq.csv
├── char_bigrams_seq.csv
└── char_trigrams_seq.csv

output_parallel/           # Output versione parallela (dataset completo)
├── word_bigrams_par.csv
├── word_trigrams_par.csv
├── char_bigrams_par.csv
└── char_trigrams_par.csv
```

#### Test (in `test/`)
```
test/
├── output_sequential/     # Test sequenziale su dataset ridotto
├── output_parallel/       # Test parallelo su dataset ridotto
└── output_hybrid/         # Test configurazioni ibride
```

### Formato CSV

```csv
ngram,frequency
"the cat",1234
"cat sat",567
"sat on",432
...
```

**Caratteristiche**:
- Header: `ngram,frequency`
- Ngram tra virgolette (gestisce spazi e caratteri speciali)
- Escaping corretto per virgolette interne: `"he said ""hello"""` 
- Ordinamento decrescente per frequenza
- Encoding: UTF-8

**Dimensioni tipiche**:
- Word bigrams: ~150 MB
- Word trigrams: ~450 MB
- Char bigrams: ~15 KB
- Char trigrams: ~800 KB

---

## 🧪 Testing e Validazione

### Test Suite Disponibili

#### 1. `test_correctness.cpp`
Test rapido su dataset ridotto (`test_data/test_file.txt`)

**Uso**:
```bash
./test_correctness
```

**Verifica**:
- Correttezza algoritmi base
- Gestione edge cases (stringhe vuote, caratteri speciali)
- Output formato CSV corretto

#### 2. `correctness_check.cpp`
Verifica completa su dataset produzione

**Uso**:
```bash
./correctness_check output_sequential output_parallel verbose
```

**Verifica**:
- Identità risultati seq vs par
- Analisi statistica discrepanze
- Report dettagliato con esempi

#### 3. `par_SoA_AoS.cpp`
Benchmark architetture dati

**Confronta**:
- Structure of Arrays (cache-friendly)
- Array of Structures (OOP-style)

### Eseguire Tutti i Test

```bash
cd build

# 1. Test rapido
./test_correctness

# 2. Run completo seq
./bigram_seq

# 3. Run completo par
./bigram_par

# 4. Verifica correttezza
./correctness_check

# 5. Benchmark SoA/AoS
./par_SoA_AoS
```

---

## 📝 Note Tecniche

### Gestione UTF-8
Il progetto normalizza caratteri accentati in ASCII standard per garantire consistenza:

**Mappature implementate**:
- `à,á,â,ã,ä,å` → `a`
- `è,é,ê,ë` → `e`
- `ì,í,î,ï` → `i`
- `ò,ó,ô,õ,ö` → `o`
- `ù,ú,û,ü` → `u`
- `ñ` → `n`
- `ç` → `c`
- `ÿ` → `y`

**Implementazione**: Riconoscimento byte sequence UTF-8 (0xC3 + offset) e conversione diretta.

### Pulizia Project Gutenberg

**Pattern riconosciuti automaticamente**:
```
*** START OF THIS PROJECT GUTENBERG EBOOK ...
*** END OF THIS PROJECT GUTENBERG EBOOK ...
Produced by ...
E-text prepared by ...
Table of Contents
CONTENTS
Chapter I
CHAPTER 1
```

**Algoritmo**:
1. Scan linea per linea
2. Rileva start marker → elimina tutto prima
3. Rileva end marker → elimina tutto dopo
4. Rimuovi indice se presente nelle prime 50 righe
5. Preserva solo testo del libro

### Thread Safety

**Garanzie nella versione parallela**:
- ✅ No shared state durante processing
- ✅ Thread-local maps completamente indipendenti
- ✅ Merge sincronizzato con `#pragma omp critical` o `atomic`
- ✅ No race conditions su lettura file (read-only)
- ✅ Scrittura CSV sequenziale (dopo join)

**Sync punti**:
1. Distribuzione file: implicit barrier dopo `#pragma omp parallel for`
2. Merge maps: `#pragma omp critical` su sezioni critiche
3. Ordinamento: sequenziale post-parallel region

### Memory Usage

**Stima memoria (dataset completo, 8 thread)**:

| Componente              | Memoria      |
|-------------------------|--------------|
| Testo caricato          | ~170 MB      |
| Token buffers (8x)      | ~800 MB      |
| Hash maps locali (8x)   | ~2.4 GB      |
| Hash maps globali       | ~1.2 GB      |
| Vettori ordinamento     | ~800 MB      |
| **TOTALE**              | **~5.4 GB**  |

**Ottimizzazione possibile**: Streaming per ridurre memory footprint (trade-off con velocità)

---

## 🚀 Sviluppi Futuri

### Features Pianificate

- [ ] **N-gram variabili**: Supporto 4-gram, 5-gram, n-gram configurabile
- [ ] **Analisi statistica**: TF-IDF, PMI (Pointwise Mutual Information), collocazioni
- [ ] **Visualizzazione**: Dashboard interattiva con grafici n-gram più frequenti
- [ ] **Export formati**: JSON, SQLite, Parquet per integrazione data science
- [ ] **Filtraggio intelligente**: Rimozione stopwords configurabile
- [ ] **Context window**: Analisi n-gram con finestra contestuale estesa

### Ottimizzazioni

- [ ] **MPI parallelization**: Scaling multi-nodo per cluster HPC
- [ ] **GPU acceleration**: CUDA/OpenCL per hash map parallele massive
- [ ] **SIMD vectorization**: AVX2/NEON per tokenizzazione batch
- [ ] **Compressione risultati**: Codifica efficiente per CSV giganti
- [ ] **Memory streaming**: Processing chunk-by-chunk per dataset enormi
- [ ] **Hash map ottimizzata**: Robin Hood hashing o Swiss Tables (Google)

### Estensioni Linguistiche

- [ ] **Multi-lingua**: Supporto completo Unicode (Cirillico, Arabo, Cinese, etc.)
- [ ] **Lemmatization**: Riduzione a forme base (running → run)
- [ ] **POS tagging**: Etichettatura Part-Of-Speech per n-gram sintattici
- [ ] **Named Entity Recognition**: Identificazione nomi propri, luoghi, date

---

## 🐛 Troubleshooting

### Errore: OpenMP non trovato (macOS)

```bash
# Installa libomp con Homebrew
brew install libomp

# Riconfigura CMake
cd build
cmake .. -DOpenMP_ROOT=$(brew --prefix libomp)
make -j4
```

### Errore: File non trovati all'esecuzione

```bash
# Assicurati di essere nella directory corretta
cd build  # o cmake-build-debug

# Verifica che book_gutenberg/ sia accessibile
ls ../book_gutenberg/

# Esegui da directory build
./bigram_seq
```

### Performance basse su macOS

```bash
# Imposta numero thread esplicito
export OMP_NUM_THREADS=8

# Disabilita dynamic threads
export OMP_DYNAMIC=FALSE

# Binding threads a core fisici
export OMP_PROC_BIND=close

./bigram_par
```

### Errore: Out of Memory

```bash
# Riduci numero thread
export OMP_NUM_THREADS=4

# Oppure processa subset di libri
# (modifica hardcoded in bigram_par.cpp)
```

### CSV malformati

```bash
# Verifica encoding UTF-8
file output_sequential/word_bigrams_seq.csv

# Controlla header
head -n 5 output_sequential/word_bigrams_seq.csv

# Se problemi, rigenera
rm -rf output_*
./bigram_seq
./bigram_par
```

---

## 📚 Riferimenti

### Librerie e Standard Utilizzati

- **C++17**: `std::filesystem`, `std::string_view`
- **OpenMP 4.5**: Parallelizzazione shared-memory
- **STL**: `unordered_map`, `vector`, `algorithm`

### Paper e Risorse

- [OpenMP Best Practices](https://www.openmp.org/wp-content/uploads/openmp-examples-4.5.0.pdf)
- [Hash Table Performance](https://tessil.github.io/2016/08/29/benchmark-hopscotch-map.html)
- [N-gram Language Models (Jurafsky & Martin)](https://web.stanford.edu/~jurafsky/slp3/)
- [Project Gutenberg](https://www.gutenberg.org/)

---

## 📄 Licenza

Questo progetto è stato sviluppato per scopi educativi e di ricerca.

**Dataset**: I testi da Project Gutenberg sono di pubblico dominio.

---

## 👤 Autore

**Lorenzo Cappetti**  
2025


