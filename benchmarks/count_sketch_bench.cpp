// =============================================================================
// count_sketch_bench.cpp — Benchmarks for probds::CountSketch
// =============================================================================

#include "probds/count_sketch.hpp"
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
// CountSketch Benchmarks
// =============================================================================

static void BM_CountSketchInsert(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto keys = generate_keys(n, 42);

    probds::CountSketch<std::uint64_t> cs(0.01, 0.01);

    std::size_t idx = 0;
    for (auto _ : state) {
        cs.insert(keys[idx % n], 1);
        ++idx;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_CountSketchInsert)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kNanosecond);

static void BM_CountSketchEstimate(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto keys = generate_keys(n, 42);

    probds::CountSketch<std::uint64_t> cs(0.01, 0.01);
    for (std::size_t i = 0; i < std::min(n, std::size_t{100000}); ++i) {
        cs.insert(keys[i], 1);
    }

    std::size_t idx = 0;
    for (auto _ : state) {
        benchmark::DoNotOptimize(cs.estimate(keys[idx % n]));
        ++idx;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_CountSketchEstimate)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kNanosecond);
