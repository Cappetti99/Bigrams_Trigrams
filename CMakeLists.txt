cmake_minimum_required(VERSION 3.12)
project(ngram-analyzer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Forza l'uso dei compilatori nativi di macOS (non quelli di Conda)
if(APPLE)
    set(CMAKE_C_COMPILER "/usr/bin/clang")
    set(CMAKE_CXX_COMPILER "/usr/bin/clang++")

    # Configurazione per OpenMP su macOS (Homebrew)
    # Assicura che libomp sia installato con: brew install libomp
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Xpreprocessor -fopenmp -std=c++17 -stdlib=libc++")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lomp")

    # Ottieni il percorso di Homebrew
    execute_process(
        COMMAND brew --prefix
        OUTPUT_VARIABLE HOMEBREW_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )

    # Include e link di libomp
    include_directories(${HOMEBREW_PREFIX}/opt/libomp/include)
    link_directories(${HOMEBREW_PREFIX}/opt/libomp/lib)

    # Ottimizzazione per Apple Silicon
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mcpu=apple-m1")
    endif()
else()
    # Linux o altre piattaforme
    find_package(OpenMP REQUIRED)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fopenmp")
endif()

# Flags di ottimizzazione
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")

# Eseguibili principali (solo quelli che esistono)
add_executable(bigram_seq bigram_seq.cpp)
add_executable(bigram_par bigram_par.cpp)
add_executable(correctness_check correctness_check.cpp)
add_executable(par_SoA_AoS par_SoA_AoS.cpp)
add_executable(test_correctness test_correctness.cpp)
add_executable(test_scheduling test_scheduling.cpp)
add_executable(test_pattern_RMvsCM test_pattern_RMvsCM.cpp)

# Link delle librerie necessarie
if(APPLE)
    target_link_libraries(bigram_seq omp pthread)
    target_link_libraries(bigram_par omp pthread)
    target_link_libraries(par_SoA_AoS omp pthread)
    target_link_libraries(test_correctness omp pthread)
    target_link_libraries(correctness_check omp pthread)
    target_link_libraries(test_scheduling omp pthread)
    target_link_libraries(test_pattern_RMvsCM omp pthread)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(bigram_seq OpenMP::OpenMP_CXX Threads::Threads)
    target_link_libraries(bigram_par OpenMP::OpenMP_CXX Threads::Threads)
    target_link_libraries(par_SoA_AoS OpenMP::OpenMP_CXX Threads::Threads)
    target_link_libraries(test_correctness OpenMP::OpenMP_CXX Threads::Threads)
    target_link_libraries(correctness_check OpenMP::OpenMP_CXX Threads::Threads)
    target_link_libraries(test_scheduling OpenMP::OpenMP_CXX Threads::Threads)
    target_link_libraries(test_scheduling_aos OpenMP::OpenMP_CXX Threads::Threads)
    target_link_libraries(test_pattern_RMvsCM OpenMP::OpenMP_CXX Threads::Threads)
endif()

# Target personalizzati
add_custom_target(download-sample
    COMMAND curl -L -o ${CMAKE_SOURCE_DIR}/alice.txt https://www.gutenberg.org/files/11/11-0.txt
    COMMENT "Downloading Alice in Wonderland from Project Gutenberg..."
)

add_custom_target(run-seq
    COMMAND $<TARGET_FILE:bigram_seq> ${CMAKE_SOURCE_DIR}/book_gutenberg/libro_11.txt
    DEPENDS bigram_seq
    COMMENT "Running bigram_seq with sample book..."
)

add_custom_target(run-par
    COMMAND $<TARGET_FILE:bigram_par> ${CMAKE_SOURCE_DIR}/book_gutenberg/libro_11.txt
    DEPENDS bigram_par
    COMMENT "Running bigram_par with sample book..."
)

add_custom_target(run-test
    COMMAND $<TARGET_FILE:test_correctness>
    DEPENDS test_correctness
    COMMENT "Running automated correctness test..."
)

# Install targets (solo quelli esistenti)
install(TARGETS bigram_seq bigram_par par_SoA_AoS test_correctness correctness_check test_scheduling test_pattern_RMvsCM DESTINATION bin)

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
