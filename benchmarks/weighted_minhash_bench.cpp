// =============================================================================
// weighted_minhash_bench.cpp — Benchmarks for probds::WeightedMinHash
// =============================================================================

#include "probds/weighted_minhash.hpp"
#include <benchmark/benchmark.h>
#include <random>
#include <vector>
#include <cmath>

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

static std::vector<double> generate_weights(std::size_t n, std::uint64_t seed) {
    std::vector<double> weights(n);
    std::mt19937_64 gen(seed);
    std::uniform_real_distribution<double> dist(0.1, 100.0);
    for (std::size_t i = 0; i < n; ++i) {
        weights[i] = dist(gen);
    }
    return weights;
}

// =============================================================================
// WeightedMinHash Benchmarks
// =============================================================================

static void BM_WeightedMinHashInsert(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    // Limit n for the insert benchmark to avoid timeouts on extremely slow paths
    // But still parameterize correctly.
    const auto keys = generate_keys(n, 42);
    const auto weights = generate_weights(n, 1337);

    for (auto _ : state) {
        probds::WeightedMinHash<std::uint64_t> wmh(200);
        for (std::size_t i = 0; i < n; ++i) {
            wmh.insert(keys[i], weights[i]);
        }
        benchmark::DoNotOptimize(wmh);
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(BM_WeightedMinHashInsert)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kMillisecond);

static void BM_WeightedMinHashJaccard(benchmark::State& state) {
    const auto k = static_cast<std::size_t>(state.range(0));
    probds::WeightedMinHash<std::uint64_t> wmh1(k);
    probds::WeightedMinHash<std::uint64_t> wmh2(k);

    for (std::uint64_t i = 0; i < 500; ++i) {
        wmh1.insert(i, 1.5 + (i % 3));
        wmh2.insert(i, 2.0 + (i % 2));
    }

    for (auto _ : state) {
        double sim = wmh1.jaccard_similarity(wmh2);
        benchmark::DoNotOptimize(sim);
    }
}
BENCHMARK(BM_WeightedMinHashJaccard)->RangeMultiplier(2)->Range(128, 1024);
