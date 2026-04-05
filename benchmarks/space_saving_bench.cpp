// =============================================================================
// space_saving_bench.cpp — Benchmarks for probds::SpaceSaving
// =============================================================================

#include "probds/space_saving.hpp"
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
// Space-Saving Benchmarks
// =============================================================================

static void BM_SpaceSavingInsert(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto keys = generate_keys(n, 42);

    for (auto _ : state) {
        probds::SpaceSaving<std::uint64_t> ss(100);
        for (const auto& key : keys) {
            ss.insert(key);
        }
        benchmark::DoNotOptimize(ss);
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(BM_SpaceSavingInsert)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kMillisecond);
