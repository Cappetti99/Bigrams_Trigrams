#!/bin/bash

#============================================================================
# Script per compilare ed eseguire i programmi del progetto Bigrams_Trigrams
#============================================================================

# Colori per output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Percorsi
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$PROJECT_DIR/cmake-build-debug"

# Funzione per stampare messaggi colorati
print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

# Funzione per compilare il progetto
compile() {
    print_info "Compilazione del progetto..."

    # Crea la directory di build se non esiste
    if [ ! -d "$BUILD_DIR" ]; then
        print_info "Creazione directory di build..."
        mkdir -p "$BUILD_DIR"
    fi

    cd "$BUILD_DIR" || exit 1

    # Esegui CMake
    print_info "Esecuzione di CMake..."
    cmake .. -DCMAKE_BUILD_TYPE=Release

    if [ $? -ne 0 ]; then
        print_error "CMake fallito!"
        exit 1
    fi

    # Compila usando cmake --build (funziona con qualsiasi generatore)
    print_info "Compilazione in corso..."
    cmake --build . --config Release -j $(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

    if [ $? -ne 0 ]; then
        print_error "Compilazione fallita!"
        exit 1
    fi

    print_success "Compilazione completata!"
    cd "$PROJECT_DIR" || exit 1
}

# Funzione per eseguire il programma sequenziale
run_seq() {
    print_info "Esecuzione del programma SEQUENZIALE..."

    if [ ! -f "$BUILD_DIR/seq" ]; then
        print_error "Eseguibile 'seq' non trovato. Compila prima il progetto."
        exit 1
    fi

    "$BUILD_DIR/seq"
}

# Funzione per eseguire il programma parallelo
run_parallel() {
    print_info "Esecuzione del programma PARALLELO..."

    if [ ! -f "$BUILD_DIR/parallel" ]; then
        print_error "Eseguibile 'parallel' non trovato. Compila prima il progetto."
        exit 1
    fi

    "$BUILD_DIR/parallel"
}

# Funzione per eseguire il programma parallelo SOA
run_parallel_soa() {
    print_info "Esecuzione del programma PARALLELO SOA..."

    if [ ! -f "$BUILD_DIR/parallel_soa" ]; then
        print_error "Eseguibile 'parallel_soa' non trovato. Compila prima il progetto."
        exit 1
    fi

    "$BUILD_DIR/parallel_soa"
}

# Funzione per eseguire il test di correttezza
run_test() {
    print_info "Esecuzione del TEST DI CORRETTEZZA..."

    if [ ! -f "$BUILD_DIR/test_correctness" ]; then
        print_error "Eseguibile 'test_correctness' non trovato. Compila prima il progetto."
        exit 1
    fi

    "$BUILD_DIR/test_correctness"
}

# Funzione per eseguire il controllo di correttezza
run_correctness_check() {
    print_info "Esecuzione del CONTROLLO DI CORRETTEZZA..."

    if [ ! -f "$BUILD_DIR/correctness_check" ]; then
        print_error "Eseguibile 'correctness_check' non trovato. Compila prima il progetto."
        exit 1
    fi

    "$BUILD_DIR/correctness_check"
}

# Funzione per pulire i file di build
clean() {
    print_info "Pulizia dei file di build..."

    if [ -d "$BUILD_DIR" ]; then
        rm -rf "$BUILD_DIR"
        print_success "File di build rimossi!"
    else
        print_warning "Directory di build non trovata."
    fi
}

# Funzione per mostrare l'aiuto
show_help() {
    echo "============================================"
    echo "  Script di esecuzione Bigrams_Trigrams"
    echo "============================================"
    echo ""
    echo "Uso: ./run.sh [comando]"
    echo ""
    echo "Comandi disponibili:"
    echo "  compile              - Compila il progetto"
    echo "  seq                  - Esegue il programma sequenziale"
    echo "  parallel             - Esegue il programma parallelo"
    echo "  parallel_soa         - Esegue il programma parallelo SOA"
    echo "  test                 - Esegue i test di correttezza"
    echo "  check                - Esegue il controllo di correttezza"
    echo "  clean                - Rimuove i file di build"
    echo "  all                  - Compila e esegue entrambi (seq + parallel)"
    echo "  help                 - Mostra questo messaggio di aiuto"
    echo ""
    echo "Esempi:"
    echo "  ./run.sh compile       # Compila il progetto"
    echo "  ./run.sh parallel      # Esegue la versione parallela"
    echo "  ./run.sh parallel_soa  # Esegue la versione parallela SOA"
    echo "  ./run.sh all           # Compila ed esegue tutto"
    echo ""
}

# Main
case "${1:-help}" in
    compile)
        compile
        ;;
    seq)
        if [ ! -f "$BUILD_DIR/seq" ]; then
            compile
        fi
        run_seq
        ;;
    parallel)
        if [ ! -f "$BUILD_DIR/parallel" ]; then
            compile
        fi
        run_parallel
        ;;
    parallel_soa)
        if [ ! -f "$BUILD_DIR/parallel_soa" ]; then
            compile
        fi
        run_parallel_soa
        ;;
    test)
        if [ ! -f "$BUILD_DIR/test_correctness" ]; then
            compile
        fi
        run_test
        ;;
    check)
        if [ ! -f "$BUILD_DIR/correctness_check" ]; then
            compile
        fi
        run_correctness_check
        ;;
    clean)
        clean
        ;;
    all)
        compile
        echo ""
        run_seq
        echo ""
        run_parallel
        ;;
    help|--help|-h)
        show_help
        ;;
    *)
        print_error "Comando sconosciuto: $1"
        echo ""
        show_help
        exit 1
        ;;
esac
