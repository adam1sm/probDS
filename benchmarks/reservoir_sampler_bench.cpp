// =============================================================================
// reservoir_sampler_bench.cpp — Benchmarks for probds::ReservoirSampler
// =============================================================================

#include "probds/reservoir_sampler.hpp"
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
// Reservoir Sampler Benchmarks
// =============================================================================

static void BM_ReservoirSamplerInsert(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto keys = generate_keys(n, 42);

    for (auto _ : state) {
        probds::ReservoirSampler<std::uint64_t> sampler(100);
        for (const auto& key : keys) {
            sampler.insert(key);
        }
        benchmark::DoNotOptimize(sampler);
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(BM_ReservoirSamplerInsert)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kMillisecond);
