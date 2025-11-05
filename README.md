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

### Tipi di N-gram Analizzati

- **Word Bigrams**: coppie di parole consecutive (es. "the cat", "cat sat")
- **Word Trigrams**: triple di parole consecutive (es. "the cat sat")
- **Character Bigrams**: coppie di caratteri consecutivi (es. "th", "he")
- **Character Trigrams**: triple di caratteri consecutivi (es. "the", "cat")

---

## Architettura del Codice

### Componenti Principali

#### 1. **TextCleaner**
Rimuove metadata di Project Gutenberg:
- Header con licenze e informazioni
- Footer con disclaimer
- Indice dei contenuti (Contents)

#### 2. **Tokenizer**
Normalizza e tokenizza il testo:
- Conversione a minuscolo
- Rimozione numeri
- Gestione caratteri UTF-8 (accenti → lettere base)
- Rimozione/sostituzione punteggiatura
- Tokenizzazione in parole o caratteri

#### 3. **NgramExtractor**
Estrae n-gram da sequenze di token:
- Template generico per `std::string` e `char`
- Costruzione efficiente delle chiavi
- Conteggio frequenze inline

#### 4. **FrequencyCounter**
Gestisce le mappe di frequenza:
- Merge di risultati parziali
- Ordinamento per frequenza
- Export in formato CSV

#### 5. **OpenMPProcessor** (solo parallelo)
Orchestrazione parallela:
- Distribuzione libri tra thread
- Mappe locali per thread (evita contention)
- Merge parallelo dei risultati

---

## Compilazione

### Requisiti
- **C++17** o superiore
- **CMake** 3.12+
- **OpenMP** (su macOS: `brew install libomp`)
- **GCC/Clang** con supporto OpenMP

### Build
```bash
mkdir build && cd build
cmake ..
make -j4
```

Oppure con CLion:
```bash
cmake --build cmake-build-debug --target all -j 4
```

### Eseguibili Generati
- `bigram_seq` - Versione sequenziale
- `bigram_par` - Versione parallela (OpenMP)
- `correctness_check` - Verifica correttezza seq vs par

---

## Utilizzo

### 1. Esecuzione Versione Sequenziale
```bash
./bigram_seq
```
Output: `output_sequential/*.csv`

### 2. Esecuzione Versione Parallela
```bash
./bigram_par
```
Output: `output_parallel/*.csv`

### 3. Verifica Correttezza
```bash
./correctness_check
```
Confronta i CSV generati e verifica che seq e parallel producano risultati identici.

---

## Risultati e Performance

### Dataset
- **80 libri** da Project Gutenberg
- **~170 MB** di testo totale
- **~48 milioni** di caratteri processati
- **~11 milioni** di word tokens

### Performance (Apple M1 Pro, 8 thread)

| Versione             | Tempo | Speedup |
|----------------------|-------|---------|
| Sequenziale          | ~110s | 1.0x |
| Parallela (8 thread) | ~27s  | **4.1x** |

### Risultati Effettivi

#### Word Bigrams
- **2.2M** n-gram unici
- **11.3M** occorrenze totali
- Top 5: "of the" (71,108), "in the" (47,011), "to the" (32,617), "to be" (22,296), "and the" (22,190)

#### Word Trigrams
- **6.7M** n-gram unici
- **11.3M** occorrenze totali
- Top 5: "i don t" (3,057), "out of the" (2,715), "one of the" (2,702), "i do not" (1,843), "it was a" (1,804)

#### Character Bigrams
- **664** n-gram unici
- **48.2M** occorrenze totali
- Top 5: "th" (1,572,497), "he" (1,376,347), "er" (865,018), "in" (851,976), "an" (825,943)

#### Character Trigrams
- **11,743** n-gram unici
- **48.2M** occorrenze totali
- Top 5: "the" (938,358), "and" (449,906), "ing" (325,729), "her" (279,347), "tha" (233,770)

---

## ✅ Verifica Correttezza

Il programma `correctness_check` verifica che le versioni sequenziale e parallela producano **risultati identici**:

```
╔═══════════════════════════════════════════════════════╗
║        ✓✓✓ CORRECTNESS CHECK PASSED ✓✓✓               ║
║   La versione parallela produce risultati IDENTICI    ║
║              alla versione sequenziale!               ║
╚═══════════════════════════════════════════════════════╝
```

Verifica:
- **Numero di n-gram unici** identico
- **Frequenze** identiche per ogni n-gram
- **Nessun n-gram mancante** o extra

---

## 🔧 Ottimizzazioni Implementate

### Versione Parallela

1. **Thread-local hash maps**
   - Ogni thread ha mappe private (no lock/contention)
   - Merge solo alla fine

2. **Dynamic scheduling**
   - Bilanciamento automatico del carico
   - Libri di dimensioni diverse distribuiti equamente

3. **Merge gerarchico**
   - Riduzione logaritmica (non lineare)
   - Parallelizzazione del merge stesso

4. **Memory pre-allocation**
   - Reserve preventivo per hash maps
   - Riduzione di rehashing

5. **Lettura binaria ottimizzata**
   - `ios::binary | ios::ate` per dimensione file
   - Singola `read()` invece di getline multipli

### Entrambe le Versioni

1. **Lookup table per tolower**
   - Array statico invece di chiamate a funzione
   - ~2x più veloce di `std::tolower()`

2. **UTF-8 inline processing**
   - Gestione accenti senza librerie esterne
   - Conversione diretta à→a, é→e, etc.

3. **Reserve string capacity**
   - Pre-allocazione basata su dimensione input
   - Evita riallocazioni multiple

---

## 📁 Struttura File Output

```
output_sequential/
├── word_bigrams_seq.csv
├── word_trigrams_seq.csv
├── char_bigrams_seq.csv
└── char_trigrams_seq.csv

output_parallel/
├── word_bigrams_par.csv
├── word_trigrams_par.csv
├── char_bigrams_par.csv
└── char_trigrams_par.csv
```

Formato CSV:
```csv
ngram,frequency
"the cat",1234
"cat sat",567
...
```

---

## 🛠️ Sviluppi Futuri

- [ ] Supporto n-gram di dimensione variabile (4-gram, 5-gram)
- [ ] Analisi statistica avanzata (TF-IDF, PMI)
- [ ] Visualizzazione interattiva dei risultati
- [ ] Parallelizzazione MPI per cluster multi-nodo
- [ ] GPU acceleration con CUDA/OpenCL
- [ ] Compressione intelligente dei risultati

---

## 📝 Note Tecniche

### Gestione UTF-8
Il progetto normalizza caratteri accentati in ASCII:
- `à,á,â,ã,ä,å` → `a`
- `è,é,ê,ë` → `e`
- `ñ` → `n`
- Etc.

### Pulizia Project Gutenberg
Rimuove automaticamente:
- `*** START OF THIS PROJECT GUTENBERG...`
- `*** END OF THIS PROJECT GUTENBERG...`
- Indici e metadati all'inizio dei libri

### Thread Safety
La versione parallela è completamente thread-safe:
- No shared state durante il processing
- Merge sincronizzato con OpenMP parallel for
- No race conditions

---
