// =============================================================================
// dd_sketch_bench.cpp — Benchmarks for probds::DDSketch
// =============================================================================

#include "probds/dd_sketch.hpp"
#include <benchmark/benchmark.h>
#include <random>
#include <vector>

// =============================================================================
// Helpers
// =============================================================================

static std::vector<double> generate_data(std::size_t n, std::uint64_t seed) {
    std::vector<double> data(n);
    std::mt19937_64 gen(seed);
    std::normal_distribution<double> dist(100.0, 50.0);
    for (std::size_t i = 0; i < n; ++i) {
        data[i] = dist(gen);
    }
    return data;
}

// =============================================================================
// DDSketch Benchmarks
// =============================================================================

static void BM_DDSketchInsert(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto data = generate_data(n, 42);

    for (auto _ : state) {
        probds::DDSketch sketch(0.01);
        for (double v : data) {
            sketch.insert(v);
        }
        benchmark::DoNotOptimize(sketch);
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(BM_DDSketchInsert)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kMillisecond);
