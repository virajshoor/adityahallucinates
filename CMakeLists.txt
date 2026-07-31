cmake_minimum_required(VERSION 3.16)
project(adityahallucinates LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -Wpedantic -O3 -march=native -flto)
add_link_options(-flto)

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
  src/uci/uci.cpp
)

add_library(engine_lib STATIC ${ENGINE_SOURCES})
target_include_directories(engine_lib PUBLIC src)

add_executable(aditya src/main.cpp)
target_link_libraries(aditya PRIVATE engine_lib)
set_target_properties(aditya PROPERTIES OUTPUT_NAME "aditya")

enable_testing()
add_executable(perft_tests tests/perft_tests.cpp)
target_link_libraries(perft_tests PRIVATE engine_lib)
add_test(NAME perft COMMAND perft_tests)

add_executable(bench_search tests/bench_search.cpp)
target_link_libraries(bench_search PRIVATE engine_lib)
