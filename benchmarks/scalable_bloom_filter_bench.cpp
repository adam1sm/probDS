// =============================================================================
// scalable_bloom_filter_bench.cpp — Benchmarks for probds::ScalableBloomFilter
// =============================================================================

#include "probds/scalable_bloom_filter.hpp"
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
// Scalable Bloom Filter Benchmarks
// =============================================================================

static void BM_ScalableBloomInsert(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto keys = generate_keys(n, 42);

    for (auto _ : state) {
        probds::ScalableBloomFilter<std::uint64_t> sbf(1000, 0.01);
        for (const auto& key : keys) {
            sbf.insert(key);
        }
        benchmark::DoNotOptimize(sbf);
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(BM_ScalableBloomInsert)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kMillisecond);

static void BM_ScalableBloomLookupPositive(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto keys = generate_keys(n, 42);

    probds::ScalableBloomFilter<std::uint64_t> sbf(1000, 0.01);
    for (const auto& key : keys) {
        sbf.insert(key);
    }

    std::size_t idx = 0;
    for (auto _ : state) {
        benchmark::DoNotOptimize(sbf.possibly_contains(keys[idx % n]));
        ++idx;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_ScalableBloomLookupPositive)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kNanosecond);

static void BM_ScalableBloomLookupNegative(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto member_keys = generate_keys(n, 42);
    const auto non_member_keys = generate_keys(n, 43);

    probds::ScalableBloomFilter<std::uint64_t> sbf(1000, 0.01);
    for (const auto& key : member_keys) {
        sbf.insert(key);
    }

    std::size_t idx = 0;
    for (auto _ : state) {
        benchmark::DoNotOptimize(sbf.possibly_contains(non_member_keys[idx % n]));
        ++idx;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_ScalableBloomLookupNegative)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kNanosecond);
