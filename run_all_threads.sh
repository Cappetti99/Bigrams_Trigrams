#!/bin/bash
#
# Script per eseguire parallel e parallel_soa con diversi numeri di thread
# e salvare tutti i risultati
#

# Configurazione
THREADS=(1 2 4 8 12 16 32 40 50 60 70 80 90 100)
ALL_EXECUTABLES=("parallel" "parallel_soa")
PROJECT_DIR="/home/lollo/CLionProjects/Bigrams_Trigrams"

# Gestione parametro opzionale
if [ -n "$1" ]; then
    if [ "$1" == "parallel" ] || [ "$1" == "parallel_soa" ]; then
        EXECUTABLES=("$1")
    else
        echo "Uso: $0 [parallel|parallel_soa]"
        echo "  Senza parametri: esegue entrambi"
        exit 1
    fi
else
    EXECUTABLES=("${ALL_EXECUTABLES[@]}")
fi

# Colori per output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Funzione per stampare con timestamp
log() {
    echo -e "${BLUE}[$(date '+%Y-%m-%d %H:%M:%S')]${NC} $1"
}

# Funzione per eseguire benchmark per un eseguibile
run_benchmark() {
    local EXEC_NAME=$1
    local EXECUTABLE="$PROJECT_DIR/cmake-build-debug/$EXEC_NAME"
    local RESULTS_BASE="$PROJECT_DIR/results/$EXEC_NAME"
    local SUMMARY_FILE="$PROJECT_DIR/results/${EXEC_NAME}_threads_summary.txt"

    echo ""
    echo -e "${CYAN}════════════════════════════════════════════════════════════════════${NC}"
    echo -e "${CYAN}  BENCHMARK: $EXEC_NAME${NC}"
    echo -e "${CYAN}════════════════════════════════════════════════════════════════════${NC}"
    echo ""

    # Verifica che l'eseguibile esista
    if [ ! -f "$EXECUTABLE" ]; then
        echo -e "${RED}Errore: Eseguibile non trovato: $EXECUTABLE${NC}"
        echo "Compila prima con: cd cmake-build-debug && make $EXEC_NAME"
        return 1
    fi

    # Crea cartella results se non esiste
    mkdir -p "$RESULTS_BASE"

    # Inizializza file di riepilogo
    echo "╔═══════════════════════════════════════════════════════════════════╗" > "$SUMMARY_FILE"
    echo "║          RIEPILOGO ESECUZIONI MULTI-THREAD ($EXEC_NAME)           ║" >> "$SUMMARY_FILE"
    echo "║          $(date '+%Y-%m-%d %H:%M:%S')                                      ║" >> "$SUMMARY_FILE"
    echo "╠═══════════════════════════════════════════════════════════════════╣" >> "$SUMMARY_FILE"
    echo "║ Threads │    Mean (s)   │    Min (s)    │    Max (s)    │ CV (%)  ║" >> "$SUMMARY_FILE"
    echo "╠═════════╪═══════════════╪═══════════════╪═══════════════╪═════════╣" >> "$SUMMARY_FILE"

    local TOTAL_RUNS=${#THREADS[@]}
    local CURRENT_RUN=0

    for T in "${THREADS[@]}"; do
        CURRENT_RUN=$((CURRENT_RUN + 1))
        
        log "${YELLOW}[$CURRENT_RUN/$TOTAL_RUNS]${NC} $EXEC_NAME con ${GREEN}$T thread${NC}..."
        
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
            cp "$RESULTS_BASE/performance_stats.csv" "$THREAD_DIR/" 2>/dev/null
            
            # Estrai statistiche dal file performance_stats.csv
            if [ -f "$THREAD_DIR/performance_stats.csv" ]; then
                MEAN=$(grep "^wall_mean," "$THREAD_DIR/performance_stats.csv" | cut -d',' -f2)
                MIN=$(grep "^wall_min," "$THREAD_DIR/performance_stats.csv" | cut -d',' -f2)
                MAX=$(grep "^wall_max," "$THREAD_DIR/performance_stats.csv" | cut -d',' -f2)
                CV=$(grep "^wall_cv," "$THREAD_DIR/performance_stats.csv" | cut -d',' -f2)
                
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
    done

    # Chiudi tabella riepilogo
    echo "╚═════════╧═══════════════╧═══════════════╧═══════════════╧═════════╝" >> "$SUMMARY_FILE"

    echo ""
    echo -e "Riepilogo $EXEC_NAME salvato in: ${BLUE}$SUMMARY_FILE${NC}"
    cat "$SUMMARY_FILE"
}

# Main
echo ""
echo -e "${GREEN}╔═══════════════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║     BENCHMARK MULTI-THREAD - Bigrams & Trigrams               ║${NC}"
echo -e "${GREEN}║     Executables: ${EXECUTABLES[*]}${NC}"
echo -e "${GREEN}║     Thread counts: ${THREADS[*]}${NC}"
echo -e "${GREEN}╚═══════════════════════════════════════════════════════════════╝${NC}"

# Esegui benchmark per ogni eseguibile
for EXEC in "${EXECUTABLES[@]}"; do
    run_benchmark "$EXEC"
done

# Riepilogo finale
echo ""
echo -e "${GREEN}╔═══════════════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║                    BENCHMARK COMPLETATO!                      ║${NC}"
echo -e "${GREEN}╚═══════════════════════════════════════════════════════════════╝${NC}"
echo ""
echo -e "Risultati salvati in:"
echo -e "  - ${BLUE}$PROJECT_DIR/results/parallel/${NC}"
echo -e "  - ${BLUE}$PROJECT_DIR/results/parallel_soa/${NC}"
echo ""
echo -e "Riepiloghi:"
echo -e "  - ${BLUE}$PROJECT_DIR/results/parallel_threads_summary.txt${NC}"
echo -e "  - ${BLUE}$PROJECT_DIR/results/parallel_soa_threads_summary.txt${NC}"
