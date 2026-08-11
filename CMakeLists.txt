cmake_minimum_required(VERSION 3.16)
project(adityahallucinates LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -O3 -march=native -flto -DNDEBUG)
add_link_options(-flto)

find_package(Threads REQUIRED)

# Fathom Syzygy (C99) — compile with gcc, no LTO (clang LTO bitcode won't link with g++)
set(FATHOM_DIR ${CMAKE_SOURCE_DIR}/third_party/Fathom/src)
add_library(fathom STATIC ${FATHOM_DIR}/tbprobe.c)
set_source_files_properties(${FATHOM_DIR}/tbprobe.c PROPERTIES LANGUAGE C)
target_include_directories(fathom PUBLIC ${FATHOM_DIR})
set_target_properties(fathom PROPERTIES
  C_STANDARD 99
  LINKER_LANGUAGE C
  INTERPROCEDURAL_OPTIMIZATION FALSE
)
target_compile_options(fathom PRIVATE -O3 -DNDEBUG -fno-lto)
# Force gcc if default C compiler is clang
if (CMAKE_C_COMPILER_ID MATCHES "Clang")
  set(CMAKE_C_COMPILER gcc CACHE STRING "" FORCE)
endif()

set(ENGINE_SOURCES
  src/board/bitboard.cpp
  src/board/board.cpp
  src/board/zobrist.cpp
  src/movegen/movegen.cpp
  src/movegen/magics.cpp
  src/eval/eval.cpp
  src/search/tt.cpp
  src/search/search.cpp
  src/search/book.cpp
  src/nnue/nnue.cpp
  src/syzygy/syzygy.cpp
  src/uci/uci.cpp
)

add_library(engine_lib STATIC ${ENGINE_SOURCES})
target_include_directories(engine_lib PUBLIC src ${FATHOM_DIR})
target_link_libraries(engine_lib PUBLIC Threads::Threads fathom)

add_executable(aditya src/main.cpp)
target_link_libraries(aditya PRIVATE engine_lib)
set_target_properties(aditya PROPERTIES OUTPUT_NAME "aditya")

enable_testing()
add_executable(perft_tests tests/perft_tests.cpp)
target_link_libraries(perft_tests PRIVATE engine_lib)
add_test(NAME perft COMMAND perft_tests)

add_executable(bench_search tests/bench_search.cpp)
target_link_libraries(bench_search PRIVATE engine_lib)
