# Parallel Bigram and Trigram Analysis: Performance Optimization with OpenMP

A high-performance C++17 system for extracting, analyzing, and ranking **word** and **character** **bigrams** and **trigrams** from large textual corpora. This project focuses on **performance engineering**, **parallel optimization**, and **scalability analysis** using OpenMP, demonstrating strong speedup up to 8–12 threads and evaluating memory-bound performance characteristics.

## Overview

This system implements a complete pipeline for n-gram analysis over a corpus of approximately **1500 books** (~600 MB) from Project Gutenberg. The project provides:

- **Sequential baseline** implementation for performance comparison
- **Two parallel OpenMP implementations**:
  - **AoS (Array of Structures)**: Standard parallel implementation with cache-aligned sharded maps
  - **SoA (Structure of Arrays)**: Optimized version with improved data locality and reduced allocations
- **Comprehensive benchmarking framework** with automated multi-thread testing
- **Performance analysis tools** for scalability, speedup, and efficiency evaluation

### Pipeline Architecture

**Load → Gutenberg Cleaning → Normalize → Tokenize → Generate N-grams → Count → Sort → Export**

Each stage is carefully optimized to minimize allocations, improve cache locality, and reduce synchronization overhead in parallel execution.

---

## Key Features

### Performance Characteristics

- **Strong scalability**: ~6.5× speedup at 12 threads on a 6-core/12-thread AMD Ryzen 5 7600X
- **Memory-bound analysis**: Demonstrates the impact of file I/O and memory bandwidth saturation beyond 12–16 threads
- **Reproducible benchmarks**: Multiple runs with warm-up phases, computing mean/min/max/std/CV (<1% variation)
- **SoA optimization benefits**: Systematic improvement through better data locality and reduced dynamic allocations

### Parallel Design Highlights

- **Dynamic OpenMP scheduling** to handle variable book sizes and reduce load imbalance
- **Thread-local buffers** for preprocessing (loading, cleaning, normalization, tokenization) with no synchronization
- **Sharded hash maps** (configurable shard count) for concurrent n-gram counting with minimal contention
- **Arena allocators per shard** with `std::string_view` keys for stable references and reduced heap allocations
- **Zero-copy tokenization** using `string_view` slices over normalized buffers

---

## Table of Contents

- [Dataset Description](#dataset-description)
- [Project Structure](#project-structure)
- [Technical Architecture](#technical-architecture)
- [Build Requirements](#build-requirements)
- [Build Instructions](#build-instructions)
- [Running the Programs](#running-the-programs)
- [Benchmark Methodology](#benchmark-methodology)
- [Performance Results](#performance-results)
- [Correctness Verification](#correctness-verification)
- [Implementation Details](#implementation-details)
- [Troubleshooting](#troubleshooting)

---

## Dataset Description

The corpus consists of nearly **1500 digital books** from [Project Gutenberg](https://www.gutenberg.org/), totaling over **600 MB** of text. Project Gutenberg offers more than 70,000 works in the public domain or released under open licenses, making it ideal for large-scale computational linguistic analysis.

### Dataset Characteristics

- **Diversity**: Multiple authors, genres, and historical periods
- **Size**: Sufficient for robust statistics with low sensitivity to noise
- **Format**: Plain text (`.txt`) files with standardized encoding
- **Acquisition**: Automated download script (`downloadBook.py`) ensures reproducible corpus building

### Dataset Setup

⚠️ **Important**: The `book_gutenberg/` directory is **NOT included in the git repository** due to its large size (~600 MB).

**Option 1 - Automated Download (Recommended)**:

Use the provided Python script to download ~1500 books from Project Gutenberg:

```bash
python3 tools/downloadBook.py
```

**Features**:
- Downloads 1500 books automatically (50 classics + 1450 sequential IDs)
- Skips already downloaded books (resumes interrupted downloads)
- Tries multiple mirrors and formats for reliability
- Respects server load with configurable pauses (1 second between downloads)
- Estimated time: ~25 minutes for full corpus (if not cached)
- Books saved in `book_gutenberg/libro_<ID>.txt` format

**Option 2 - Manual download**:
1. Create the `book_gutenberg/` directory:
   ```bash
   mkdir -p book_gutenberg
   ```
2. Download books from [Project Gutenberg](https://www.gutenberg.org/) in `.txt` format
3. Name files as `libro_1.txt`, `libro_2.txt`, etc., or update the path in source code
4. Find book IDs at: `https://www.gutenberg.org/ebooks/<ID>`

**Option 3 - Use your own corpus**:
- Place any collection of `.txt` files in a directory
- Update the `folder_path` variable in the source files (`seq.cpp`, `parallel.cpp`, `parallel_soa.cpp`)
- No specific naming convention required if you modify the code

---

## Project Structure

```
Bigrams_Trigrams/
├── seq.cpp                     # Sequential baseline implementation
├── parallel.cpp                # Parallel OpenMP (AoS - Array of Structures)
├── parallel_soa.cpp            # Parallel OpenMP (SoA - Structure of Arrays)
├── correctness_check.cpp       # Correctness verification tool
├── test_correctness.cpp        # Unit tests on small dataset
├── CMakeLists.txt              # Build configuration (OpenMP + cross-platform)
├── run.sh                      # Build and run helper script
├── run_all_threads.sh          # Automated benchmark across thread counts
├── analyze_results.py          # Results parser and plot generator
├── plot_results.py             # Plotting utilities
├── README.md                   # This documentation
├── .gitignore                  # Git ignore rules
│
├── book_gutenberg/             # Dataset: ~1500 Project Gutenberg books (NOT in git)
│   ├── libro_1.txt             # Download separately or use downloadBook.py
│   ├── libro_10.txt
│   └── ...
│
├── test_data/                  # Small dataset for testing/validation
│   └── test_file.txt
│
├── results/                    # Output directory (generated at runtime, NOT in git)
│   ├── sequential/             # Sequential version outputs (*.csv files)
│   ├── parallel/               # Parallel (AoS) outputs
│   └── parallel_soa/           # Parallel (SoA) outputs
│
├── build/                      # CMake build directory (generated, NOT in git)
│   ├── seq                     # Sequential executable
│   ├── parallel                # Parallel (AoS) executable
│   ├── parallel_soa            # Parallel (SoA) executable
│   └── test_correctness        # Test executable
│
├── cmake-build-debug/          # CLion build directory (generated, NOT in git)
├── charts/                     # Performance charts and plots
├── grafici_analisi/            # Additional analysis graphs
├── doc/                        # Documentation and reports
└── tools/                      # Utility scripts
```

### Important Notes on Repository Structure

⚠️ **The following directories are NOT included in the git repository** (see `.gitignore`):
- **`book_gutenberg/`**: Dataset must be downloaded separately (see [Dataset Setup](#dataset-setup))
- **`build/`**, **`cmake-build-*/`**: Generated during compilation
- **`results/`**: Created at runtime, contains CSV outputs (ignored to avoid large files in repo)
- **`*.csv`** files: All CSV outputs are gitignored

After cloning the repository, you need to:
1. Download or prepare the dataset in `book_gutenberg/`
2. Build the project to generate executables
3. Run the programs to generate `results/` output

### Key Files

- **`seq.cpp`**: Sequential baseline with single-threaded execution
- **`parallel.cpp`**: OpenMP parallel version with AoS sharded map layout
- **`parallel_soa.cpp`**: OpenMP parallel version with SoA layout for improved cache locality
- **`test_correctness.cpp`**: Correctness validation by comparing outputs
- **`CMakeLists.txt`**: Cross-platform build configuration with OpenMP support
- **`run_all_threads.sh`**: Batch benchmark runner testing multiple thread configurations
- **`analyze_results.py`**: Parses performance statistics and generates comparative plots

---

## Technical Architecture

### Processing Pipeline

The system follows a structured multi-stage pipeline:

1. **Loading**: Read raw `.txt` files from disk into memory buffers (binary mode, pre-sized allocation)
2. **Gutenberg Cleaning**: Remove Project Gutenberg headers/footers (in-place via START/END markers)
3. **Normalization**: Convert to lowercase, handle UTF-8 accents, remove/replace non-linguistic symbols
4. **Tokenization**: 
   - **Word tokens**: Extract as `string_view` slices (zero-copy) on whitespace boundaries
   - **Character tokens**: Collect all non-whitespace characters into contiguous vectors
5. **N-gram Generation**: Slide fixed-size window (n=2 for bigrams, n=3 for trigrams) over token sequences
6. **Counting**: Update frequency tables (sequential: single map; parallel: sharded maps with per-shard mutexes)
7. **Aggregation**: Combine partial counts into global statistics (implicit in sharded design)
8. **Sorting**: Rank n-grams by frequency (descending order)
9. **Export**: Write separate CSV files for each n-gram type + performance statistics

### Parallel Strategy

The parallel implementations leverage OpenMP with several optimization techniques:

#### Work Distribution
- **`#pragma omp parallel`**: Creates a team of worker threads
- **`#pragma omp for schedule(dynamic)`**: Distributes books dynamically to handle variable sizes
- Dynamic scheduling reduces load imbalance when some books are significantly larger than others

#### Thread-Local Processing
- Each thread maintains **private buffers** for:
  - Normalized text (in-place modification)
  - Word tokens (`std::vector<std::string_view>`)
  - Character tokens (`std::vector<char>`)
- **No synchronization required** during loading, cleaning, normalization, and tokenization

#### Concurrent Counting with Sharding
- Global frequency tables are split into **power-of-2 shards** (e.g., 256 shards)
- Shard selection: `shard_idx = hash(ngram) % NUM_SHARDS`
- Each shard has a **dedicated mutex** → threads only contend when updating the same shard
- **Arena allocators per shard**: Keys stored as `string_view` pointing to arena memory (stable references, reduced allocations)

#### AoS vs SoA Comparison

| Aspect | AoS (`parallel.cpp`) | SoA (`parallel_soa.cpp`) |
|--------|----------------------|--------------------------|
| **Layout** | Cache-aligned struct: `{mutex, map, arena}` | Separate arrays: `mutexes[]`, `maps[]`, `arenas[]` |
| **Cache Locality** | Components grouped together | Frequently accessed fields more compact |
| **Performance** | Baseline parallel performance | ~1–3% improvement, more visible at low thread counts |
| **Complexity** | Simpler indexing | Requires careful array management |

---

## Build Requirements

### System Requirements

- **C++ Compiler**: C++17 support required
- **CMake**: Version ≥ 3.12
- **OpenMP**: Version 4.5 or later
- **Threading Library**: POSIX threads (Linux/macOS)

### Platform-Specific Requirements

#### Linux
- GCC ≥ 7.0 or Clang ≥ 5.0 with OpenMP support
- `pthread` library (automatically linked via `Threads::Threads`)

```bash
# Ubuntu/Debian
sudo apt-get install build-essential cmake libomp-dev

# Fedora/RHEL
sudo dnf install gcc-c++ cmake libomp-devel
```

#### macOS
- Apple Clang or Homebrew GCC
- **Homebrew `libomp`** (required for OpenMP support)

```bash
# Install OpenMP via Homebrew
brew install libomp

# Optional: Install GCC for better OpenMP performance
brew install gcc
```

**Note**: The CMakeLists.txt includes macOS-specific OpenMP configuration paths for Homebrew installations.

---

## Build Instructions

### Standard Build (Release Mode)

From the repository root:

```bash
# Configure CMake (Release build with optimizations)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# Build all targets
cmake --build build -j$(nproc)
```

### Build Flags

The project uses aggressive optimization flags for benchmarking:
- **`-O3`**: Maximum optimization level
- **`-march=native`**: CPU-specific optimizations (use target architecture features)
- **`-DNDEBUG`**: Disable assertions

### Available Targets

After building, the following executables are generated in `build/`:

- **`seq`**: Sequential baseline
- **`parallel`**: Parallel version (AoS layout)
- **`parallel_soa`**: Parallel version (SoA layout)
- **`test_correctness`**: Correctness verification tool

---

## Running the Programs

### Prerequisites

⚠️ **Important**: Update the dataset path in the source files before running:
- The folder path is currently hard-coded in `main()` of each implementation
- Default: `folder_path = ".../book_gutenberg"`
- Modify to point to your actual dataset location or implement CLI argument parsing

### 1. Sequential Baseline

```bash
cd build
./seq
```

**Output**: Results written to `results/sequential/` (directory created automatically)
- `word_bigrams.csv`
- `word_trigrams.csv`
- `char_bigrams.csv`
- `char_trigrams.csv`
- Performance statistics file

> **Note**: All CSV files and `results/` directory are gitignored. Output files are generated locally on each run.

### 2. Parallel (AoS)

```bash
./parallel
```

The program prompts for thread count interactively:
```
Enter number of threads: 8
```

Or provide via stdin:
```bash
echo 8 | ./parallel
```

**Output**: Results written to `results/parallel/` (directory created automatically)

> **Note**: CSV outputs are not tracked by git (see `.gitignore`)

### 3. Parallel (SoA)

```bash
./parallel_soa
```

Similarly prompts for thread count:
```bash
echo 12 | ./parallel_soa
```

**Output**: Results written to `results/parallel_soa/` (directory created automatically)

### 4. Automated Benchmarking

Run benchmarks across multiple thread configurations:

```bash
./run_all_threads.sh
```

This script:
- Tests thread counts: 1, 2, 4, 8, 12, 16, 32, 40, 50, 60, 70, 80, 90, 100
- Executes each configuration multiple times (including warm-up runs)
- Saves results for analysis

---

## Benchmark Methodology

### Measurement Protocol

To ensure reliable and reproducible performance measurements:

1. **Multiple Runs**: Each configuration executed 12 times
2. **Warm-up Phase**: First 2 runs discarded to mitigate cold-cache effects
3. **Measured Runs**: Remaining 10 runs used for statistics
4. **Metrics Collected**:
   - **Wall-clock time**: Total execution time (including I/O)
   - **CPU time**: Actual CPU computation time
   - **Mean, Min, Max**: Statistical aggregation
   - **Standard Deviation & CV**: Measurement stability (typically <1%)

### Data Structure Reinitialization

Between runs, all hash maps and buffers are cleared and re-initialized to ensure:
- Clean state for each benchmark
- Consistent memory allocation patterns
- Comparable measurements across runs

### Performance Stability

The **Coefficient of Variation (CV)** consistently remains **below 1%**, indicating:
- High measurement stability
- Minimal run-to-run variance
- Absence of significant OS scheduling noise
- Consistent cache behavior

---

## Performance Results

> **📊 For comprehensive performance analysis, scalability curves, and detailed experimental results, see the technical report:**  
> **[doc/Report_Bigram_Cappetti.pdf](doc/Report_Bigram_Cappetti.pdf)**

### Summary of Key Findings

**Experimental Setup**: AMD Ryzen 5 7600X (6 cores/12 threads), 32 GB RAM, GCC 13.3.0 with `-O3 -march=native`

**Performance Highlights**:
- **Sequential baseline**: 104.93 seconds (CV: 0.53%)
- **Best parallel performance**: ~15.4 seconds with 50+ threads
- **Peak speedup**: ~6.8× (at optimal thread count)
- **Strong scalability**: Near-linear up to 8–12 threads
- **SoA advantage**: Consistent 1–3% improvement over AoS

### Scalability Characteristics

| Thread Count | Behavior | Key Observation |
|--------------|----------|----------------|
| **1–12** | Strong scaling | Execution time: 123s → 16s; embarrassingly parallel |
| **12–50** | Plateau region | Marginal gains; memory bandwidth becomes bottleneck |
| **50+** | Saturation | No further improvement; fully memory-bound |

### Primary Bottleneck

**File I/O and Memory Bandwidth Saturation**:
- Each thread must load entire book into memory before processing
- Memory bandwidth saturates beyond 12–16 threads
- Further parallelism cannot improve I/O-bound operations
- Computation itself is highly parallelizable; limiting factor is data loading

### Detailed Analysis Available

The technical report includes:
- Complete performance tables with all thread configurations
- Speedup curves with ideal scaling comparison
- Parallel efficiency analysis
- Memory-bound behavior characterization
- Amdahl's Law interpretation
- Future optimization strategies

---

## Correctness Verification

The project includes comprehensive correctness testing to ensure parallel implementations produce identical results to the sequential baseline.

### Running Tests

```bash
# Build test executable
cmake --build build

# Run correctness tests
./build/test_correctness

# Or use CMake target
cmake --build build --target run-test
```

### Verification Strategy

The correctness checker:
1. Runs all three implementations (sequential, parallel, parallel_soa) on a small test dataset
2. Compares output CSV files for exact matches:
   - Same n-grams extracted
   - Identical frequency counts
   - Consistent ordering
3. Validates that parallel aggregation is deterministic
4. Ensures thread-safety of sharded map operations

### What is Verified

✓ **Functional correctness**: All implementations produce the same n-grams  
✓ **Counting accuracy**: Frequency tables match exactly  
✓ **Determinism**: Repeated runs yield identical results  
✓ **Thread-safety**: No race conditions or data corruption  
✓ **Output format consistency**: CSV structure and content identical

---

## Implementation Details

### Text Normalization

**Purpose**: Transform raw text into standardized representation for consistent tokenization

**Operations**:
1. **Case folding**: Convert all characters to lowercase
2. **Symbol handling**: Remove or replace punctuation, digits, and special characters
3. **UTF-8 processing**: Map accented characters to uniform form (e.g., é → e)

**Critical aspects**:
- Performed **in-place** to avoid intermediate allocations
- UTF-8 aware to prevent corrupting multibyte sequences
- Balance between aggressive filtering (removes information) and conservative rules (leaves noise)

### Tokenization

**Purpose**: Convert normalized text into token sequences for n-gram extraction

**Implementation**:
- **Word tokenization**: Split on whitespace, extract as `std::string_view` slices (zero-copy)
- **Character tokenization**: Collect all non-whitespace characters into contiguous vector
- **Buffer pre-allocation**: Reserve capacity upfront to minimize reallocations
- **Boundary handling**: Careful treatment of apostrophes, hyphens, contractions

**Efficiency considerations**:
- Zero-copy design reduces memory pressure
- Contiguous storage improves cache locality during sliding window
- Thread-local buffers eliminate synchronization overhead

### N-gram Generation

**Method**: Sliding window of size *n* over token sequence

**Process**:
- For bigrams (n=2): Extract all consecutive pairs `(token[i], token[i+1])`
- For trigrams (n=3): Extract all consecutive triples `(token[i], token[i+1], token[i+2])`
- **Key construction**: Reuse thread-local string buffers to minimize allocations
- **Boundary correctness**: Avoid creating n-grams across document boundaries

**Counting mechanism**:
- **Sequential**: Direct updates to `std::unordered_map<std::string, size_t>`
- **Parallel**: Sharded maps with hash-based shard selection and per-shard locking

### Sharded Map Architecture

**Design goals**:
- Minimize lock contention during concurrent updates
- Maintain stable key references throughout execution
- Reduce memory allocations for new n-gram keys

**Implementation**:
```cpp
// Simplified structure (AoS version)
struct Shard {
    std::mutex mutex;
    std::unordered_map<std::string_view, size_t> map;
    ArenaAllocator arena;
};

std::vector<Shard> shards(NUM_SHARDS);

// Insert or increment
size_t shard_idx = hash(ngram) % NUM_SHARDS;
std::lock_guard lock(shards[shard_idx].mutex);
auto it = shards[shard_idx].map.find(ngram);
if (it != shards[shard_idx].map.end()) {
    it->second++;
} else {
    // Allocate key in arena, insert with count 1
    string_view key = shards[shard_idx].arena.allocate(ngram);
    shards[shard_idx].map[key] = 1;
}
```

**Shard count selection**: Power of 2 (e.g., 256) for efficient modulo via bitwise AND

### Arena Allocator

**Purpose**: Reduce heap allocation overhead and maintain stable string references

**Mechanism**:
- Pre-allocate large memory blocks (e.g., 1 MB chunks)
- Bump-pointer allocation within blocks
- No individual deallocations (memory freed at program end)
- Keys stored as `string_view` pointing into arena memory

**Benefits**:
- **Faster than malloc**: Simple pointer increment vs complex heap management
- **Better locality**: Related keys stored contiguously
- **Stable references**: `string_view` keys remain valid (no reallocation/moves)

### SoA Memory Layout Optimization

**Motivation**: Improve cache utilization by separating frequently accessed fields

**AoS Layout** (traditional):
```cpp
struct Shard {
    std::mutex mutex;           // 40 bytes (cache line padding)
    std::unordered_map map;     // 56 bytes
    ArenaAllocator arena;       // 32 bytes
};  // Total: ~128 bytes per shard
```

**SoA Layout** (optimized):
```cpp
std::vector<std::mutex> mutexes;
std::vector<std::unordered_map> maps;
std::vector<ArenaAllocator> arenas;
```

**Advantages**:
- Mutexes stored contiguously → better cache line utilization during lock acquisition
- Maps accessed sequentially during iteration → fewer cache misses
- Separation allows different access patterns for different components

**Trade-offs**:
- Slightly more complex indexing logic
- Requires careful array management
- Benefits more pronounced under high concurrency

---

## Troubleshooting

### OpenMP Not Found (Linux)

**Error**: `Could NOT find OpenMP_C (missing: OpenMP_C_FLAGS OpenMP_C_LIB_NAMES)`

**Solution**:
```bash
# Ubuntu/Debian
sudo apt-get install libomp-dev

# Fedora/RHEL  
sudo dnf install libomp-devel

# Arch Linux
sudo pacman -S openmp
```

### macOS OpenMP Issues

**Error**: `ld: library not found for -lomp`

**Solution**:
```bash
# Install libomp via Homebrew
brew install libomp

# Reconfigure CMake to detect Homebrew paths
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# If still not found, explicitly set paths:
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DOpenMP_C_FLAGS="-Xpreprocessor -fopenmp -I/opt/homebrew/opt/libomp/include" \
  -DOpenMP_C_LIB_NAMES="omp" \
  -DOpenMP_CXX_FLAGS="-Xpreprocessor -fopenmp -I/opt/homebrew/opt/libomp/include" \
  -DOpenMP_CXX_LIB_NAMES="omp" \
  -DOpenMP_omp_LIBRARY=/opt/homebrew/opt/libomp/lib/libomp.dylib
```

### Dataset Path Issues

**Error**: `Error: Cannot open folder ...` or `Folder not found`

**Solution**:
1. Update the hard-coded `folder_path` in `main()` of each `.cpp` file
2. Point to your actual dataset location
3. Ensure the path uses correct separators for your OS
4. **Recommended**: Refactor code to accept path as command-line argument:
   ```cpp
   int main(int argc, char* argv[]) {
       std::string folder_path = (argc > 1) ? argv[1] : "./book_gutenberg";
       // ...
   }
   ```

### Performance Variance

**Issue**: Large variation in benchmark results (CV > 2%)

**Solutions**:
- Run on an idle machine (close background applications)
- Disable CPU frequency scaling:
  ```bash
  # Linux
  sudo cpupower frequency-set --governor performance
  ```
- Pin thread affinity:
  ```bash
  export OMP_PROC_BIND=TRUE
  export OMP_PLACES=cores
  ```
- Use local SSD for dataset (avoid network drives or slow HDDs)
- Increase number of measured runs in benchmark code

### Build Warnings

**Warning**: `-march=native` may not be portable

**Solution**: For portable builds, replace in CMakeLists.txt:
```cmake
# Instead of:
target_compile_options(target PRIVATE -march=native)

# Use:
target_compile_options(target PRIVATE -mtune=generic)
```

---

## Results Analysis Tools

Python scripts for automated performance analysis:

```bash
# Parse results and generate plots
python3 analyze_results.py

# Quick visualization with sample data
python3 plot_results.py
```

**Generated plots**: execution time, speedup curves, parallel efficiency, statistical summaries

> See the [technical report](doc/Report_Bigram_Cappetti.pdf) for detailed interpretation of all performance metrics

---

## Future Work

### Performance Optimizations

1. **I/O Optimization**:
   - Memory-mapped files (`mmap`) to reduce copying overhead
   - Overlapped I/O with computation using asynchronous file operations
   - Batch processing with prefetching to hide latency

2. **Aggregation Strategies**:
   - Hybrid approach: thread-local maps + final merge for reduced contention
   - Lock-free hash tables for contention-free concurrent updates
   - Hierarchical aggregation to reduce memory footprint

3. **Key Representation**:
   - Token ID encoding instead of string concatenation
   - Perfect hashing for fixed vocabulary
   - Compressed trie structures for memory efficiency

### Analytical Extensions

1. **Advanced Statistics**:
   - Conditional probabilities P(word₂|word₁)
   - Pointwise Mutual Information (PMI) for association strength
   - TF-IDF weighting for n-gram importance

2. **Filtering and Enrichment**:
   - Stop-word filtering to focus on meaningful patterns
   - Part-of-speech tagging for syntactic n-grams
   - Document metadata integration (author, genre, period)

3. **Higher-Order N-grams**:
   - Extend to 4-grams and 5-grams
   - Variable-length n-gram extraction
   - Skip-gram models for non-contiguous patterns

### Portability and Usability

1. **Command-Line Interface**:
   - Flexible dataset path specification
   - Configurable output formats (JSON, binary, database)
   - Runtime parameter tuning (shard count, buffer sizes)

2. **Cross-Platform Testing**:
   - Windows MSVC support
   - ARM architecture optimization
   - GPU acceleration exploration (CUDA/OpenCL)

3. **Integration**:
   - Python bindings for data science workflows
   - REST API for web-based analysis
   - Streaming mode for real-time text processing

---

## Documentation

### Technical Report

For comprehensive technical discussion, see:

**📄 [doc/Report_Bigram_Cappetti.pdf](doc/Report_Bigram_Cappetti.pdf)**

**Contents**:
- Complete pipeline architecture and design rationale
- Detailed implementation of normalization, tokenization, and n-gram generation
- Sharded map architecture with arena allocators
- AoS vs SoA memory layout comparison
- Full experimental results with scalability analysis
- Speedup curves, efficiency plots, and statistical analysis
- Memory bandwidth saturation study
- Amdahl's Law interpretation for memory-bound workloads
- Future optimization strategies

---

## License and Attribution

### Project License

This implementation is developed as an academic project focusing on performance engineering and parallel optimization techniques.

### Dataset Attribution

The textual corpus is sourced from [Project Gutenberg](https://www.gutenberg.org/):
- All texts are in the **public domain** or released under open licenses
- Comply with Project Gutenberg's [Terms of Use](https://www.gutenberg.org/policy/terms_of_use.html) for redistribution
- Individual book licenses may vary; check specific book metadata

### Third-Party Libraries

- **OpenMP**: OpenMP ARB specification v4.5 ([openmp.org](https://www.openmp.org/))
- **C++ Standard Library**: ISO C++17 standard

---

## Contributing

This project is primarily for educational and research purposes. For questions, improvements, or collaboration:

**Author**: Lorenzo Cappetti   
**Contact**: lorenzo.cappetti@edu.unifi.it

### Suggestions Welcome

- Performance optimization ideas
- Alternative parallelization strategies
- Cross-platform compatibility improvements
- Additional analysis metrics

---

## Acknowledgments

- **Project Gutenberg** for providing the extensive public-domain textual corpus
- **OpenMP Architecture Review Board** for the parallelization framework
- **University of Florence** for computational resources and academic support

---

**Last Updated**: January 2026
