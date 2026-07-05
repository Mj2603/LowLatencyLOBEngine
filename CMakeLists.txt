cmake_minimum_required(VERSION 3.16)
project(low_latency_lob_engine VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_library(lob_engine_lib STATIC
    src/engine.cpp
)

target_include_directories(lob_engine_lib PUBLIC src)

target_compile_options(lob_engine_lib PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:-Wall;-Wextra;-Wpedantic>
    $<$<CXX_COMPILER_ID:Clang>:-Wall;-Wextra;-Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

target_compile_definitions(lob_engine_lib PRIVATE LOB_ENGINE_STATIC)

add_executable(lob_engine
    src/main.cpp
)

target_link_libraries(lob_engine PRIVATE lob_engine_lib)

target_compile_options(lob_engine PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:-Wall;-Wextra;-Wpedantic>
    $<$<CXX_COMPILER_ID:Clang>:-Wall;-Wextra;-Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

add_executable(benchmark_lob
    benchmarks/benchmark_main.cpp
)

target_link_libraries(benchmark_lob PRIVATE lob_engine_lib)

target_compile_options(benchmark_lob PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:-Wall;-Wextra;-Wpedantic>
    $<$<CXX_COMPILER_ID:Clang>:-Wall;-Wextra;-Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

enable_testing()
add_subdirectory(tests)
