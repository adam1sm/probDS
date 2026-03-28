// =============================================================================
// count_min_log_bench.cpp — Benchmarks for probds::CountMinLog
// =============================================================================

#include "probds/count_min_log.hpp"
#include <benchmark/benchmark.h>
#include <random>
#include <vector>

// =============================================================================
// Helpers
// =============================================================================

static std::vector<std::uint64_t> generate_keys(std::size_t n, std::uint64_t seed) {
    std::vector<std::uint64_t> keys(n);
    std::mt19937_64 gen(seed);
    for (std::size_t i = 0; i < n; ++i) {
        keys[i] = gen();
    }
    return keys;
}

// =============================================================================
// CountMinLog Benchmarks
// =============================================================================

static void BM_CountMinLogInsert(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto keys = generate_keys(n, 42);

    probds::CountMinLog<std::uint64_t, 4> sketch(0.01, 0.01);

    std::size_t idx = 0;
    for (auto _ : state) {
        sketch.insert(keys[idx % n], 1);
        ++idx;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_CountMinLogInsert)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kNanosecond);

static void BM_CountMinLogEstimate(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto keys = generate_keys(n, 42);

    probds::CountMinLog<std::uint64_t, 4> sketch(0.01, 0.01);
    for (std::size_t i = 0; i < std::min(n, std::size_t{100000}); ++i) {
        sketch.insert(keys[i], 1);
    }

    std::size_t idx = 0;
    for (auto _ : state) {
        benchmark::DoNotOptimize(sketch.estimate(keys[idx % n]));
        ++idx;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_CountMinLogEstimate)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kNanosecond);
