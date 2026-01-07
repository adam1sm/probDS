cmake_minimum_required(VERSION 3.15)
project(probDS
    VERSION 0.1.0
    DESCRIPTION "High-performance probabilistic data structures library"
    LANGUAGES CXX
)

# ---- C++17 standard ----
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---- Compiler warnings and optimization flags ----
add_compile_options(-Wall -Wextra -Wpedantic -O3 -march=native)

# ---- Header-only library (INTERFACE target) ----
add_library(probds INTERFACE)
target_include_directories(probds INTERFACE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
)

# =============================================================================
# Tests (Google Test)
# =============================================================================
option(PROBDS_BUILD_TESTS "Build probDS tests" ON)

if(PROBDS_BUILD_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)
    include(GoogleTest)

    # Unified test binary
    add_executable(probds_tests
        tests/bloom_filter_test.cpp
        tests/cuckoo_filter_test.cpp
        tests/hyperloglog_test.cpp
        tests/count_min_sketch_test.cpp
        tests/counting_bloom_filter_test.cpp
        tests/scalable_bloom_filter_test.cpp
        tests/blocked_bloom_filter_test.cpp
        tests/quotient_filter_test.cpp
        tests/minhash_test.cpp
        tests/simhash_test.cpp
        tests/tdigest_test.cpp
        tests/kll_sketch_test.cpp
        tests/misra_gries_test.cpp
        tests/space_saving_test.cpp
        tests/ams_sketch_test.cpp
        tests/reservoir_sampler_test.cpp
        tests/lossy_counting_test.cpp
        tests/binary_fuse8_test.cpp
        tests/heavy_keeper_test.cpp
        tests/dd_sketch_test.cpp
        tests/xor_filter_test.cpp
        tests/ribbon_filter_test.cpp
        tests/count_min_log_test.cpp
        tests/count_sketch_test.cpp
        tests/exponential_histogram_test.cpp
        tests/weighted_minhash_test.cpp
        tests/bbit_minhash_test.cpp
        tests/odd_sketch_test.cpp
        tests/learned_bloom_filter_test.cpp
        tests/concurrent_test.cpp
        tests/params_test.cpp
    )
    target_link_libraries(probds_tests PRIVATE probds GTest::gtest GTest::gtest_main)
    gtest_discover_tests(probds_tests)
endif()

# =============================================================================
# Benchmarks (Google Benchmark)
# =============================================================================
option(PROBDS_BUILD_BENCHMARKS "Build probDS benchmarks" ON)

if(PROBDS_BUILD_BENCHMARKS)
    find_package(benchmark REQUIRED)

    # Unified benchmark binary
    add_executable(probds_benchmarks
        benchmarks/bloom_filter_bench.cpp
        benchmarks/cuckoo_filter_bench.cpp
        benchmarks/hyperloglog_bench.cpp
        benchmarks/count_min_sketch_bench.cpp
        benchmarks/counting_bloom_filter_bench.cpp
        benchmarks/scalable_bloom_filter_bench.cpp
        benchmarks/blocked_bloom_filter_bench.cpp
        benchmarks/quotient_filter_bench.cpp
        benchmarks/minhash_bench.cpp
        benchmarks/simhash_bench.cpp
        benchmarks/tdigest_bench.cpp
        benchmarks/kll_sketch_bench.cpp
        benchmarks/misra_gries_bench.cpp
        benchmarks/space_saving_bench.cpp
        benchmarks/ams_sketch_bench.cpp
        benchmarks/reservoir_sampler_bench.cpp
        benchmarks/lossy_counting_bench.cpp
        benchmarks/binary_fuse8_bench.cpp
        benchmarks/heavy_keeper_bench.cpp
        benchmarks/dd_sketch_bench.cpp
        benchmarks/xor_filter_bench.cpp
        benchmarks/ribbon_filter_bench.cpp
        benchmarks/count_min_log_bench.cpp
        benchmarks/count_sketch_bench.cpp
        benchmarks/exponential_histogram_bench.cpp
        benchmarks/weighted_minhash_bench.cpp
        benchmarks/bbit_minhash_bench.cpp
        benchmarks/odd_sketch_bench.cpp
        benchmarks/learned_bloom_filter_bench.cpp
        benchmarks/concurrent_bench.cpp
    )
    target_link_libraries(probds_benchmarks PRIVATE probds benchmark::benchmark benchmark::benchmark_main)
endif()

# =============================================================================
# CLI Parameter Calculator Tool
# =============================================================================
add_executable(param_calculator tools/param_calculator.cpp)
