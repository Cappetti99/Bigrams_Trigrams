cmake_minimum_required(VERSION 3.12)
project(ngram-analyzer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 🔧 Forza l'uso dei compilatori nativi di macOS (non quelli di Conda)
if(APPLE)
    set(CMAKE_C_COMPILER "/usr/bin/clang")
    set(CMAKE_CXX_COMPILER "/usr/bin/clang++")

    # ✅ Configurazione per OpenMP su macOS (Homebrew)
    # Assicura che libomp sia installato con: brew install libomp
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Xpreprocessor -fopenmp -std=c++17 -stdlib=libc++")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lomp")

    # Ottieni il percorso di Homebrew
    execute_process(
            COMMAND brew --prefix
            OUTPUT_VARIABLE HOMEBREW_PREFIX
            OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    # Include e link di libomp
    include_directories(${HOMEBREW_PREFIX}/opt/libomp/include)
    link_directories(${HOMEBREW_PREFIX}/opt/libomp/lib)

    # Ottimizzazione per Apple Silicon
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mcpu=apple-m1")
    endif()
else()
    # ✅ Linux o altre piattaforme
    find_package(OpenMP REQUIRED)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fopenmp")
endif()

# Flags di ottimizzazione
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")

# 🔹 Eseguibile per bigram_seq
add_executable(bigram_seq bigram_seq.cpp)

# 🔹 Eseguibile per bigram_par
add_executable(bigram_par bigram_par.cpp)

# 🔹 Eseguibile per correctness_check
add_executable(correctness_check correctness_check.cpp)

# Link delle librerie necessarie per bigram_seq
if(APPLE)
    target_link_libraries(bigram_seq omp "-pthread")
    target_link_libraries(bigram_par omp "-pthread")
    # correctness_check non ha bisogno di OpenMP
else()
    target_link_libraries(bigram_seq OpenMP::OpenMP_CXX)
    target_link_libraries(bigram_par OpenMP::OpenMP_CXX)
endif()

# Link delle librerie necessarie per bigram_par
if(APPLE)
    target_link_libraries(bigram_par omp "-pthread")
else()
    target_link_libraries(bigram_par OpenMP::OpenMP_CXX)
    find_package(Threads REQUIRED)
    target_link_libraries(bigram_par Threads::Threads)
endif()

# Target personalizzati
add_custom_target(download-sample
        COMMAND curl -L -o alice.txt https://www.gutenberg.org/files/11/11-0.txt
        COMMENT "Downloading Alice in Wonderland from Project Gutenberg..."
)

add_custom_target(run-seq
        COMMAND bigram_seq alice.txt
        DEPENDS bigram_seq
        COMMENT "Running bigram_seq with alice.txt..."
)

add_custom_target(run-par
        COMMAND bigram_par alice.txt
        DEPENDS bigram_par
        COMMENT "Running bigram_par with alice.txt..."
)

# Install targets
install(TARGETS bigram_seq bigram_par DESTINATION bin)

# Print configuration info
message(STATUS "")
message(STATUS "==================== Configuration ====================")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
if(APPLE)
    message(STATUS "Homebrew prefix: ${HOMEBREW_PREFIX}")
    message(STATUS "OpenMP include: ${HOMEBREW_PREFIX}/opt/libomp/include")
    message(STATUS "OpenMP lib: ${HOMEBREW_PREFIX}/opt/libomp/lib")
endif()
message(STATUS "======================================================")
message(STATUS "")