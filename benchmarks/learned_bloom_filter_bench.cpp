// =============================================================================
// learned_bloom_filter_bench.cpp — Benchmarks for probds::LearnedBloomFilter
// =============================================================================

#include "probds/learned_bloom_filter.hpp"
#include <benchmark/benchmark.h>
#include <random>
#include <vector>
#include <string>

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

// Mock classifier: classifies half of the keys as positive (even keys)
static auto mock_classifier = [](std::uint64_t key) noexcept {
    return (key & 1) == 0;
};

using LBFType = probds::LearnedBloomFilter<std::uint64_t, decltype(mock_classifier)>;

// =============================================================================
// LearnedBloomFilter Benchmarks
// =============================================================================

static void BM_LearnedBloomFilterInsert(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto keys = generate_keys(n, 42);

    for (auto _ : state) {
        // Assume half are false negatives to size backup bloom filter
        LBFType filter(mock_classifier, 0.01, n / 2);
        for (const auto& key : keys) {
            filter.insert(key);
        }
        benchmark::DoNotOptimize(filter);
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(BM_LearnedBloomFilterInsert)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kMillisecond);

static void BM_LearnedBloomFilterLookupPositive(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto keys = generate_keys(n, 42);

    LBFType filter(mock_classifier, 0.01, n / 2);
    for (const auto& key : keys) {
        filter.insert(key);
    }

    for (auto _ : state) {
        for (const auto& key : keys) {
            bool res = filter.possibly_contains(key);
            benchmark::DoNotOptimize(res);
        }
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(BM_LearnedBloomFilterLookupPositive)
    ->Arg(10000)
    ->Arg(1000000)
    ->Unit(benchmark::kMillisecond);
