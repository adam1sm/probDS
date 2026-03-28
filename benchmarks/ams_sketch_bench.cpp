// =============================================================================
// ams_sketch_bench.cpp — Benchmarks for probds::AMSSketch
// =============================================================================

#include "probds/ams_sketch.hpp"
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
// AMS Sketch Benchmarks
// =============================================================================

static void BM_AMSSketchInsert(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto keys = generate_keys(n, 42);

    for (auto _ : state) {
        probds::AMSSketch<std::uint64_t> ams(5, 64);
        for (const auto& key : keys) {
            ams.insert(key);
        }
        benchmark::DoNotOptimize(ams);
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(BM_AMSSketchInsert)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kMillisecond);

static void BM_AMSSketchEstimate(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto keys = generate_keys(n, 42);
    probds::AMSSketch<std::uint64_t> ams(5, 64);
    for (const auto& key : keys) {
        ams.insert(key);
    }

    for (auto _ : state) {
        double f2 = ams.estimate_f2();
        benchmark::DoNotOptimize(f2);
    }
}
BENCHMARK(BM_AMSSketchEstimate)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kNanosecond);
