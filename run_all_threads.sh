#!/bin/bash
#
# Script per eseguire parallel con diversi numeri di thread
# e salvare tutti i risultati
#

# Configurazione
THREADS=(1 2 4 8 12 16 32 40 50 60 70 80 90 100)
PROJECT_DIR="/home/lollo/CLionProjects/Bigrams_Trigrams"
EXECUTABLE="$PROJECT_DIR/cmake-build-debug/parallel"
RESULTS_BASE="$PROJECT_DIR/results/parallel"
SUMMARY_FILE="$PROJECT_DIR/results/all_threads_summary.txt"

# Colori per output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Funzione per stampare con timestamp
log() {
    echo -e "${BLUE}[$(date '+%Y-%m-%d %H:%M:%S')]${NC} $1"
}

# Verifica che l'eseguibile esista
if [ ! -f "$EXECUTABLE" ]; then
    echo -e "${RED}Errore: Eseguibile non trovato: $EXECUTABLE${NC}"
    echo "Compila prima con: cd cmake-build-debug && make parallel"
    exit 1
fi

# Crea cartella results se non esiste
mkdir -p "$RESULTS_BASE"

# Inizializza file di riepilogo
echo "╔═══════════════════════════════════════════════════════════════════╗" > "$SUMMARY_FILE"
echo "║          RIEPILOGO ESECUZIONI MULTI-THREAD                        ║" >> "$SUMMARY_FILE"
echo "║          $(date '+%Y-%m-%d %H:%M:%S')                                      ║" >> "$SUMMARY_FILE"
echo "╠═══════════════════════════════════════════════════════════════════╣" >> "$SUMMARY_FILE"
echo "║ Threads │    Mean (s)   │    Min (s)    │    Max (s)    │ CV (%)  ║" >> "$SUMMARY_FILE"
echo "╠═════════╪═══════════════╪═══════════════╪═══════════════╪═════════╣" >> "$SUMMARY_FILE"

echo ""
echo -e "${GREEN}╔═══════════════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║     BENCHMARK MULTI-THREAD - Bigrams & Trigrams               ║${NC}"
echo -e "${GREEN}║     Thread counts: ${THREADS[*]}${NC}"
echo -e "${GREEN}╚═══════════════════════════════════════════════════════════════╝${NC}"
echo ""

TOTAL_RUNS=${#THREADS[@]}
CURRENT_RUN=0

for T in "${THREADS[@]}"; do
    CURRENT_RUN=$((CURRENT_RUN + 1))
    
    log "${YELLOW}[$CURRENT_RUN/$TOTAL_RUNS]${NC} Esecuzione con ${GREEN}$T thread${NC}..."
    
    # Crea cartella per questo numero di thread
    THREAD_DIR="$RESULTS_BASE/threads_$T"
    mkdir -p "$THREAD_DIR"
    
    # Esegui il programma
    START_TIME=$(date +%s)
    echo "$T" | "$EXECUTABLE" > "$THREAD_DIR/output.log" 2>&1
    EXIT_CODE=$?
    END_TIME=$(date +%s)
    DURATION=$((END_TIME - START_TIME))
    
    if [ $EXIT_CODE -eq 0 ]; then
        # Copia i risultati nella cartella specifica per thread
        cp "$RESULTS_BASE/word_bigrams.csv" "$THREAD_DIR/" 2>/dev/null
        cp "$RESULTS_BASE/word_trigrams.csv" "$THREAD_DIR/" 2>/dev/null
        cp "$RESULTS_BASE/char_bigrams.csv" "$THREAD_DIR/" 2>/dev/null
        cp "$RESULTS_BASE/char_trigrams.csv" "$THREAD_DIR/" 2>/dev/null
        cp "$RESULTS_BASE/performance_stats.txt" "$THREAD_DIR/" 2>/dev/null
        
        # Estrai statistiche dal file performance_stats.txt
        if [ -f "$THREAD_DIR/performance_stats.txt" ]; then
            MEAN=$(grep "Mean:" "$THREAD_DIR/performance_stats.txt" | head -1 | awk '{print $2}')
            MIN=$(grep "Minimum:" "$THREAD_DIR/performance_stats.txt" | head -1 | awk '{print $2}')
            MAX=$(grep "Maximum:" "$THREAD_DIR/performance_stats.txt" | head -1 | awk '{print $2}')
            CV=$(grep "Coeff. Variation:" "$THREAD_DIR/performance_stats.txt" | head -1 | awk '{print $3}')
            
            # Aggiungi al riepilogo
            printf "║ %7d │ %13s │ %13s │ %13s │ %7s ║\n" "$T" "$MEAN" "$MIN" "$MAX" "$CV" >> "$SUMMARY_FILE"
            
            log "${GREEN}✓ Completato${NC} - Mean: ${MEAN}s, Min: ${MIN}s, Max: ${MAX}s (durata totale: ${DURATION}s)"
        else
            log "${YELLOW}⚠ Completato ma file stats non trovato${NC}"
            printf "║ %7d │ %13s │ %13s │ %13s │ %7s ║\n" "$T" "N/A" "N/A" "N/A" "N/A" >> "$SUMMARY_FILE"
        fi
    else
        log "${RED}✗ Errore (exit code: $EXIT_CODE)${NC} - Vedi $THREAD_DIR/output.log"
        printf "║ %7d │ %13s │ %13s │ %13s │ %7s ║\n" "$T" "ERROR" "ERROR" "ERROR" "ERROR" >> "$SUMMARY_FILE"
    fi
    
    echo ""
done

# Chiudi tabella riepilogo
echo "╚═════════╧═══════════════╧═══════════════╧═══════════════╧═════════╝" >> "$SUMMARY_FILE"

echo -e "${GREEN}╔═══════════════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║                    BENCHMARK COMPLETATO!                      ║${NC}"
echo -e "${GREEN}╚═══════════════════════════════════════════════════════════════╝${NC}"
echo ""
echo -e "Risultati salvati in: ${BLUE}$RESULTS_BASE${NC}"
echo -e "Riepilogo in: ${BLUE}$SUMMARY_FILE${NC}"
echo ""
echo "Contenuto riepilogo:"
echo ""
cat "$SUMMARY_FILE"
