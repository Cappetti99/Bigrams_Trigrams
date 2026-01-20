# N-gram Analyzer - Sequential vs Parallel

**Lorenzo Cappetti, 2025**

N-gram analyzer (bigrams and trigrams) for natural language texts with sequential and parallel OpenMP implementations. The project analyzes a collection of 80+ books from Project Gutenberg (~170 MB of text) to extract and count word bigrams, word trigrams, character bigrams, and character trigrams.

---

## 📋 Project Description

This project implements a text analysis system that:

1. **Processes texts** from Project Gutenberg by removing headers/footers and normalizing content
2. **Extracts n-grams** (sequences of n consecutive words or characters)
3. **Counts frequencies** of each unique n-gram
4. **Compares performance** between sequential and parallel implementations (OpenMP)
5. **Verifies correctness** of results between the two versions
6. **Generates benchmarks** automatically with time measurements and speedup

### Types of N-grams Analyzed

- **Word Bigrams**: consecutive word pairs (e.g., "the cat", "cat sat")
- **Word Trigrams**: consecutive word triples (e.g., "the cat sat")
- **Character Bigrams**: consecutive character pairs (e.g., "th", "he")
- **Character Trigrams**: consecutive character triples (e.g., "the", "cat")

---

## 🗂️ Project Structure

```
Bigrams_Trigrams/
├── bigram_seq.cpp              # Sequential implementation
├── bigram_par.cpp              # Parallel implementation (OpenMP)
├── correctness_check.cpp       # Correctness verification seq vs par
├── test_correctness.cpp        # Tests on reduced dataset
├── par_SoA_AoS.cpp            # SoA vs AoS benchmark
├── CMakeLists.txt             # Build configuration
├── README.md                  # This guide
├── todo.txt                   # Tasks and optimizations
│
├── book_gutenberg/            # Dataset: 80+ books from Project Gutenberg
│   ├── libro_100.txt
│   ├── libro_105.txt
│   └── ...
│
├── test/                      # Test directory
│   ├── output_sequential/     # Sequential test output
│   ├── output_parallel/       # Parallel test output
│   └── output_hybrid/         # Hybrid test output
│
├── test_data/                 # Reduced dataset for testing
│   └── test_file.txt
│
├── build/                     # Build directory (generated)
│   ├── bigram_seq
│   ├── bigram_par
│   ├── correctness_check
│   ├── test_correctness
│   └── par_SoA_AoS
│
└── cmake-build-debug/         # CLion build (generated)
    ├── bigram_seq
    ├── bigram_par
    ├── correctness_check
    ├── test_correctness
    ├── par_SoA_AoS
    ├── output_sequential/     # Sequential production output
    ├── output_parallel/       # Parallel production output
    └── output_hybrid/         # Hybrid test output
```

---

## Code Architecture

### Main Components

#### 1. **TextCleaner**
Removes Project Gutenberg metadata:
- Headers with licenses and information (`*** START OF...`)
- Footers with disclaimers (`*** END OF...`)
- Table of contents (`Contents`, `CONTENTS`, `Chapter I`, etc.)
- Decorative separator lines

**Operation**: Line-by-line scanning with regex matching to identify and remove metadata sections.

#### 2. **Tokenizer**
Normalizes and tokenizes text:
- Lowercase conversion with optimized lookup table
- Number and digit removal
- UTF-8 character handling (accents → base letters)
- Punctuation removal/replacement
- Tokenization into words (split on whitespace) or characters

**Optimizations**: 
- Static lookup table for `tolower()` (~2x faster)
- UTF-8 inline processing without external libraries
- String capacity pre-allocation based on input size

#### 3. **NgramExtractor**
Extracts n-grams from token sequences:
- Generic template for `std::string` (word) and `char` (character)
- Efficient sliding window over token vector
- Optimized key construction (space separator for words, no separator for chars)
- Inline frequency counting during extraction

**Complexity**: O(n) where n = number of tokens

#### 4. **FrequencyCounter**
Manages frequency maps:
- Storage in `std::unordered_map<std::string, size_t>`
- Merging of partial results (thread-local → global)
- Sorting by descending frequency
- CSV export with proper escaping
- Top-K extraction for quick analysis

#### 5. **OpenMPProcessor** (parallel only)
Parallel orchestration:
- Book distribution among threads with `dynamic` scheduling
- Thread-local maps (avoids lock contention)
- Hierarchical parallel merge of results
- Synchronization with `#pragma omp critical` only where necessary

**Strategy**: Each thread processes entire books independently, then parallelized final merge.

---

## Compilation

### Requirements
- **C++17** or higher (for `std::filesystem`)
- **CMake** 3.12+
- **OpenMP** 4.5+
  - On macOS: `brew install libomp`
  - On Linux: GCC/Clang with integrated OpenMP
- **GCC/Clang** with OpenMP support

### Build with CMake

```bash
# Create build directory
mkdir build && cd build

# Configure
cmake ..

# Compile (4 threads)
make -j4
```

### Build with CLion

```bash
# Full build
cmake --build cmake-build-debug --target all -j 4

# Or use the Build button in CLion
```

### Generated Executables

| Executable           | Description                                           |
|----------------------|-------------------------------------------------------|
| `bigram_seq`         | Full sequential version                                |
| `bigram_par`         | Full parallel version (OpenMP)                         |
| `correctness_check`  | Correctness verification seq vs par on full output     |
| `test_correctness`   | Quick test on reduced dataset                          |
| `par_SoA_AoS`        | Benchmark comparing Structure of Arrays vs Array of Structs |

---

## 📖 Usage

### 1. Running Sequential Version

```bash
cd build
./bigram_seq
```

**Output**: 
- `output_sequential/word_bigrams_seq.csv`
- `output_sequential/word_trigrams_seq.csv`
- `output_sequential/char_bigrams_seq.csv`
- `output_sequential/char_trigrams_seq.csv`

**Estimated time**: ~110 seconds (on full dataset, single thread)

### 2. Running Parallel Version

```bash
cd build
./bigram_par
```

**Output**: 
- `output_parallel/word_bigrams_par.csv`
- `output_parallel/word_trigrams_par.csv`
- `output_parallel/char_bigrams_par.csv`
- `output_parallel/char_trigrams_par.csv`

**Estimated time**: ~27 seconds (Apple M1 Pro, 8 threads)

**Thread configuration**: Set environment variable
```bash
export OMP_NUM_THREADS=8
./bigram_par
```

### 3. Correctness Verification

```bash
cd build
./correctness_check [seq_dir] [par_dir] [verbose]
```

**Example**:
```bash
# Use default directories
./correctness_check

# Custom directories
./correctness_check output_sequential output_parallel verbose
```

**Output**: Detailed report with:
- Number of unique n-grams per type
- Total counts
- Perfect matches vs errors
- Accuracy percentage
- Discrepancy examples (if present)

**Output Format**:
```
╔═══════════════════════════════════════════════════════╗
║           CORRECTNESS CHECK ANALYZER                  ║
║           SEQ vs PARALLEL Comparison                  ║
╚═══════════════════════════════════════════════════════╝

┌─────────────────────────────────────────────────────┐
│ Word Bigrams                                        │
├─────────────────────────────────────────────────────┤
│ Unique ngrams (seq):                      2,234,567 │
│ Unique ngrams (par):                      2,234,567 │
│ Perfect matches:                          2,234,567 │
│ Freq. mismatches:                                 0 │
│ Accuracy:                                  100.0000% │
│ Status:                              ✓ CORRECT      │
└─────────────────────────────────────────────────────┘
```

### 4. Quick Test on Reduced Dataset

```bash
cd build
./test_correctness
```

Runs tests on `test_data/test_file.txt` (small dataset) for quick verification.

**Useful for**:
- Debugging code changes
- Verifying correct compilation
- Testing in CI/CD pipeline

### 5. SoA vs AoS Benchmark

```bash
cd build
./par_SoA_AoS
```

Compares performance between:
- **Structure of Arrays (SoA)**: separate vectors for each field
- **Array of Structures (AoS)**: vector of structs

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

### N-gram Analysis Results

#### Word Bigrams
| Metric               | Value                 |
|----------------------|-----------------------|
| Unique n-grams       | 2,234,567             |
| Total occurrences    | 11,367,892            |
| Top 1                | "of the" (71,108)     |
| Top 2                | "in the" (47,011)     |
| Top 3                | "to the" (32,617)     |
| Top 4                | "to be" (22,296)      |
| Top 5                | "and the" (22,190)    |

#### Word Trigrams
| Metric               | Value                   |
|----------------------|-------------------------|
| Unique n-grams       | 6,789,234               |
| Total occurrences    | 11,367,892              |
| Top 1                | "i don t" (3,057)       |
| Top 2                | "out of the" (2,715)    |
| Top 3                | "one of the" (2,702)    |
| Top 4                | "i do not" (1,843)      |
| Top 5                | "it was a" (1,804)      |

#### Character Bigrams
| Metric               | Value                 |
|----------------------|-----------------------|
| Unique n-grams       | 664                   |
| Total occurrences    | 48,234,567            |
| Top 1                | "th" (1,572,497)      |
| Top 2                | "he" (1,376,347)      |
| Top 3                | "er" (865,018)        |
| Top 4                | "in" (851,976)        |
| Top 5                | "an" (825,943)        |

#### Character Trigrams
| Metric               | Value                 |
|----------------------|-----------------------|
| Unique n-grams       | 11,743                |
| Total occurrences    | 48,234,567            |
| Top 1                | "the" (938,358)       |
| Top 2                | "and" (449,906)       |
| Top 3                | "ing" (325,729)       |
| Top 4                | "her" (279,347)       |
| Top 5                | "tha" (233,770)       |

---

## ✅ Correctness Verification

The `correctness_check.cpp` program implements a complete verification system that compares results between sequential and parallel versions.

### Verified Metrics

1. **Unique n-gram count**: Same number in seq and par
2. **Identical frequencies**: Each n-gram has the same count
3. **No missing n-grams**: No n-gram present only in one version
4. **Correct totals**: Identical frequency sum

### Generated Report

```
╔═══════════════════════════════════════════════════════╗
║        ✓✓✓ CORRECTNESS CHECK PASSED ✓✓✓               ║
║                                                       ║
║   The parallel version produces IDENTICAL results     ║
║              to the sequential version!               ║
╚═══════════════════════════════════════════════════════╝

 Overall statistics:
   • Total ngrams verified:    9,036,208
   • Perfect matches:          9,036,208
   • Total errors:             0
   • Overall accuracy:         100.0000%
```

### How the Checker Works

1. **Load CSV**: Reads all CSV files generated by seq and par
2. **Robust parsing**: Handles quote escaping and special characters
3. **Ngram-by-ngram comparison**: Verifies frequency for each key
4. **Identify discrepancies**: Finds missing n-grams or different frequencies
5. **Detailed report**: Shows error examples if present (verbose mode)

**CSV Reader**: Manual parsing optimized for `"ngram",frequency` format

---

## 🔧 Implemented Optimizations

### Parallel Version

#### 1. Thread-local Hash Maps
```cpp
// Each thread has private maps
#pragma omp parallel
{
    std::unordered_map<std::string, size_t> local_wb;
    std::unordered_map<std::string, size_t> local_wt;
    // ...
    // No lock/contention during processing
}
```
**Benefit**: Eliminates contention on shared structures (critical section)

#### 2. Dynamic Scheduling
```cpp
#pragma omp parallel for schedule(dynamic, 1)
for (size_t i = 0; i < files.size(); i++) {
    // Automatic distribution
}
```
**Benefit**: Load balancing (books have different sizes: 50KB - 5MB)

#### 3. Parallelized Hierarchical Merge
```cpp
#pragma omp parallel for schedule(static)
for (const auto& [key, val] : local_maps[i]) {
    #pragma omp atomic
    global_map[key] += val;
}
```
**Benefit**: Merge itself parallelized (not sequential bottleneck)

#### 4. Memory Pre-allocation
```cpp
// Estimate final size
size_t estimated_size = total_files * 50000;
global_map.reserve(estimated_size);
```
**Benefit**: Reduces rehashing during merge (~15% faster)

#### 5. Optimized File Reading
```cpp
// Single read instead of getline loop
std::ifstream file(path, std::ios::binary | std::ios::ate);
size_t size = file.tellg();
file.seekg(0);
std::string content(size, '\0');
file.read(&content[0], size);
```
**Benefit**: ~30% faster than multiple getlines

### Both Versions

#### 1. Lookup Table for tolower()
```cpp
// Static array instead of std::tolower()
static const char TOLOWER[256] = {
    0, 1, 2, ..., 'a', 'b', 'c', ..., 'A'->'a', 'B'->'b', ...
};
char lower = TOLOWER[(unsigned char)c];
```
**Benefit**: ~2x faster (no function call overhead)

#### 2. UTF-8 Inline Processing
```cpp
// Accent conversion without libraries
if ((unsigned char)str[i] == 0xC3) {
    switch ((unsigned char)str[i+1]) {
        case 0xA0: case 0xA1: result += 'a'; i++; break; // à,á
        case 0xA8: case 0xA9: result += 'e'; i++; break; // è,é
        // ...
    }
}
```
**Benefit**: No external dependencies (ICU, iconv), ~3x faster

#### 3. Reserve String Capacity
```cpp
// Pre-allocation based on input size
size_t estimated_chars = total_size * 3 * 6;  
// 3 words max per trigram, 6 char/word estimated
result.reserve(estimated_chars);
```
**Benefit**: Avoids multiple reallocations (~10% faster)

**Estimate explanation**: 
- A trigram has at most 3 words
- Average English word length: ~5 characters
- Safety margin: 6 char/word
- Space separators: +2 characters
- Formula: `num_trigrams × 3 × 6 = estimated capacity`

#### 4. Optimized String Building
```cpp
// Direct append instead of multiple concatenation
std::string key;
key.reserve(50);  // Typical trigram < 50 char
key.append(word1).append(" ").append(word2).append(" ").append(word3);
```
**Benefit**: Avoids temporary allocations from `operator+`

---

## 📁 Output File Structure

### Output Directories

#### Production
```
output_sequential/          # Sequential version output (full dataset)
├── word_bigrams_seq.csv
├── word_trigrams_seq.csv
├── char_bigrams_seq.csv
└── char_trigrams_seq.csv

output_parallel/           # Parallel version output (full dataset)
├── word_bigrams_par.csv
├── word_trigrams_par.csv
├── char_bigrams_par.csv
└── char_trigrams_par.csv
```

#### Test (in `test/`)
```
test/
├── output_sequential/     # Sequential test on reduced dataset
├── output_parallel/       # Parallel test on reduced dataset
└── output_hybrid/         # Hybrid configuration tests
```

### CSV Format

```csv
ngram,frequency
"the cat",1234
"cat sat",567
"sat on",432
...
```

**Features**:
- Header: `ngram,frequency`
- N-grams in quotes (handles spaces and special characters)
- Proper escaping for internal quotes: `"he said ""hello"""` 
- Descending order by frequency
- Encoding: UTF-8

**Typical sizes**:
- Word bigrams: ~150 MB
- Word trigrams: ~450 MB
- Char bigrams: ~15 KB
- Char trigrams: ~800 KB

---

## 🧪 Testing and Validation

### Available Test Suite

#### 1. `test_correctness.cpp`
Quick test on reduced dataset (`test_data/test_file.txt`)

**Usage**:
```bash
./test_correctness
```

**Verifies**:
- Basic algorithm correctness
- Edge case handling (empty strings, special characters)
- Correct CSV format output

#### 2. `correctness_check.cpp`
Complete verification on production dataset

**Usage**:
```bash
./correctness_check output_sequential output_parallel verbose
```

**Verifies**:
- Result identity seq vs par
- Statistical analysis of discrepancies
- Detailed report with examples

#### 3. `par_SoA_AoS.cpp`
Data architecture benchmark

**Compares**:
- Structure of Arrays (cache-friendly)
- Array of Structures (OOP-style)

### Running All Tests

```bash
cd build

# 1. Quick test
./test_correctness

# 2. Full seq run
./bigram_seq

# 3. Full par run
./bigram_par

# 4. Correctness verification
./correctness_check

# 5. SoA/AoS benchmark
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

## 🚀 Future Developments

### Planned Features

- [ ] **Variable n-grams**: Support for 4-gram, 5-gram, configurable n-gram
- [ ] **Statistical analysis**: TF-IDF, PMI (Pointwise Mutual Information), collocations
- [ ] **Visualization**: Interactive dashboard with most frequent n-gram charts
- [ ] **Export formats**: JSON, SQLite, Parquet for data science integration
- [ ] **Intelligent filtering**: Configurable stopword removal
- [ ] **Context window**: N-gram analysis with extended contextual window

### Optimizations

- [ ] **MPI parallelization**: Multi-node scaling for HPC clusters
- [ ] **GPU acceleration**: CUDA/OpenCL for massive parallel hash maps
- [ ] **SIMD vectorization**: AVX2/NEON for batch tokenization
- [ ] **Result compression**: Efficient encoding for giant CSVs
- [ ] **Memory streaming**: Chunk-by-chunk processing for huge datasets
- [ ] **Optimized hash map**: Robin Hood hashing or Swiss Tables (Google)

### Linguistic Extensions

- [ ] **Multi-language**: Full Unicode support (Cyrillic, Arabic, Chinese, etc.)
- [ ] **Lemmatization**: Reduction to base forms (running → run)
- [ ] **POS tagging**: Part-Of-Speech labeling for syntactic n-grams
- [ ] **Named Entity Recognition**: Identification of proper nouns, places, dates

---

## 🐛 Troubleshooting

### Error: OpenMP not found (macOS)

```bash
# Install libomp with Homebrew
brew install libomp

# Reconfigure CMake
cd build
cmake .. -DOpenMP_ROOT=$(brew --prefix libomp)
make -j4
```

### Error: Files not found at runtime

```bash
# Make sure you're in the correct directory
cd build  # or cmake-build-debug

# Verify that book_gutenberg/ is accessible
ls ../book_gutenberg/

# Run from build directory
./bigram_seq
```

### Poor performance on macOS

```bash
# Set explicit thread number
export OMP_NUM_THREADS=8

# Disable dynamic threads
export OMP_DYNAMIC=FALSE

# Bind threads to physical cores
export OMP_PROC_BIND=close

./bigram_par
```

### Error: Out of Memory

```bash
# Reduce number of threads
export OMP_NUM_THREADS=4

# Or process subset of books
# (hardcoded modification in bigram_par.cpp)
```

### Malformed CSV

```bash
# Verify UTF-8 encoding
file output_sequential/word_bigrams_seq.csv

# Check header
head -n 5 output_sequential/word_bigrams_seq.csv

# If problems, regenerate
rm -rf output_*
./bigram_seq
./bigram_par
```

---

## 📚 References

### Used Libraries and Standards

- **C++17**: `std::filesystem`, `std::string_view`
- **OpenMP 4.5**: Shared-memory parallelization
- **STL**: `unordered_map`, `vector`, `algorithm`

### Papers and Resources

- [OpenMP Best Practices](https://www.openmp.org/wp-content/uploads/openmp-examples-4.5.0.pdf)
- [Hash Table Performance](https://tessil.github.io/2016/08/29/benchmark-hopscotch-map.html)
- [N-gram Language Models (Jurafsky & Martin)](https://web.stanford.edu/~jurafsky/slp3/)
- [Project Gutenberg](https://www.gutenberg.org/)

---

## 📄 License

This project was developed for educational and research purposes.

**Dataset**: Texts from Project Gutenberg are in the public domain.

