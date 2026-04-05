// =============================================================================
// heavy_keeper_bench.cpp — Benchmarks for probds::HeavyKeeper
// =============================================================================

#include "probds/heavy_keeper.hpp"
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
// HeavyKeeper Benchmarks
// =============================================================================

static void BM_HeavyKeeperInsert(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto keys = generate_keys(n, 42);

    for (auto _ : state) {
        // d=4, w=1024, k=100
        probds::HeavyKeeper<std::uint64_t> hk(4, 1024, 100);
        for (const auto& key : keys) {
            hk.insert(key);
        }
        benchmark::DoNotOptimize(hk);
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(BM_HeavyKeeperInsert)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kMillisecond);
