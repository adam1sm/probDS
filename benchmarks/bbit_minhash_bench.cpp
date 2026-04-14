// =============================================================================
// bbit_minhash_bench.cpp — Benchmarks for probds::BBitMinHash
// =============================================================================

#include "probds/bbit_minhash.hpp"
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
// BBitMinHash Benchmarks
// =============================================================================

static void BM_BBitMinHashInsert(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto keys = generate_keys(n, 42);

    for (auto _ : state) {
        probds::BBitMinHash<std::uint64_t> mh(200, 2);
        for (const auto& key : keys) {
            mh.insert(key);
        }
        benchmark::DoNotOptimize(mh);
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(BM_BBitMinHashInsert)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kMillisecond);

static void BM_BBitMinHashJaccard(benchmark::State& state) {
    const auto k = static_cast<std::size_t>(state.range(0));
    probds::BBitMinHash<std::uint64_t> mh1(k, 2);
    probds::BBitMinHash<std::uint64_t> mh2(k, 2);

    for (std::uint64_t i = 0; i < 500; ++i) {
        mh1.insert(i);
        mh2.insert(i + 100);
    }

    for (auto _ : state) {
        double sim = mh1.jaccard_similarity(mh2);
        benchmark::DoNotOptimize(sim);
    }
}
BENCHMARK(BM_BBitMinHashJaccard)->RangeMultiplier(2)->Range(128, 1024);
