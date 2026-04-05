// =============================================================================
// misra_gries_bench.cpp — Benchmarks for probds::MisraGries
// =============================================================================

#include "probds/misra_gries.hpp"
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
// Misra-Gries Benchmarks
// =============================================================================

static void BM_MisraGriesInsert(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto keys = generate_keys(n, 42);

    for (auto _ : state) {
        probds::MisraGries<std::uint64_t> mg(100);
        for (const auto& key : keys) {
            mg.insert(key);
        }
        benchmark::DoNotOptimize(mg);
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(BM_MisraGriesInsert)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kMillisecond);
