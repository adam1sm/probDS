// =============================================================================
// odd_sketch_bench.cpp — Benchmarks for probds::OddSketch
// =============================================================================

#include "probds/odd_sketch.hpp"
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
// OddSketch Benchmarks
// =============================================================================

static void BM_OddSketchInsert(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto keys = generate_keys(n, 42);

    for (auto _ : state) {
        probds::OddSketch<std::uint64_t> sketch(4096);
        for (const auto& key : keys) {
            sketch.insert(key);
        }
        benchmark::DoNotOptimize(sketch);
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(BM_OddSketchInsert)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kMillisecond);

static void BM_OddSketchDifference(benchmark::State& state) {
    const auto m = static_cast<std::size_t>(state.range(0));
    probds::OddSketch<std::uint64_t> sketch1(m);
    probds::OddSketch<std::uint64_t> sketch2(m);

    for (std::uint64_t i = 0; i < 1000; ++i) {
        sketch1.insert(i);
        sketch2.insert(i + 500);
    }

    for (auto _ : state) {
        double diff = sketch1.symmetric_difference(sketch2);
        benchmark::DoNotOptimize(diff);
    }
}
BENCHMARK(BM_OddSketchDifference)->RangeMultiplier(2)->Range(1024, 65536);
