// =============================================================================
// simhash_bench.cpp — Benchmarks for probds::SimHash
// =============================================================================

#include "probds/simhash.hpp"
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
// SimHash Benchmarks
// =============================================================================

static void BM_SimHashInsert(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto keys = generate_keys(n, 42);

    for (auto _ : state) {
        probds::SimHash<std::uint64_t> sh;
        for (const auto& key : keys) {
            sh.insert(key);
        }
        benchmark::DoNotOptimize(sh);
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(BM_SimHashInsert)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kMillisecond);

static void BM_SimHashGetFingerprint(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto keys = generate_keys(n, 42);
    probds::SimHash<std::uint64_t> sh;
    for (const auto& key : keys) {
        sh.insert(key);
    }

    for (auto _ : state) {
        uint64_t fp = sh.get_fingerprint();
        benchmark::DoNotOptimize(fp);
    }
}
BENCHMARK(BM_SimHashGetFingerprint)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kNanosecond);
