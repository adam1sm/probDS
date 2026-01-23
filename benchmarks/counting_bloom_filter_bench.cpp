// =============================================================================
// counting_bloom_filter_bench.cpp — Benchmarks for probds::CountingBloomFilter
// =============================================================================

#include "probds/counting_bloom_filter.hpp"
#include <benchmark/benchmark.h>
#include <random>
#include <unordered_set>
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
// Counting Bloom Filter Benchmarks
// =============================================================================

static void BM_CountingBloomInsert(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto keys = generate_keys(n, 42);

    for (auto _ : state) {
        probds::CountingBloomFilter<std::uint64_t> cbf(n, 0.01);
        for (const auto& key : keys) {
            cbf.insert(key);
        }
        benchmark::DoNotOptimize(cbf);
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(BM_CountingBloomInsert)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kMillisecond);

static void BM_CountingBloomLookupPositive(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto keys = generate_keys(n, 42);

    probds::CountingBloomFilter<std::uint64_t> cbf(n, 0.01);
    for (const auto& key : keys) {
        cbf.insert(key);
    }

    std::size_t idx = 0;
    for (auto _ : state) {
        benchmark::DoNotOptimize(cbf.possibly_contains(keys[idx % n]));
        ++idx;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_CountingBloomLookupPositive)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kNanosecond);

static void BM_CountingBloomLookupNegative(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto member_keys = generate_keys(n, 42);
    const auto non_member_keys = generate_keys(n, 43);

    probds::CountingBloomFilter<std::uint64_t> cbf(n, 0.01);
    for (const auto& key : member_keys) {
        cbf.insert(key);
    }

    std::size_t idx = 0;
    for (auto _ : state) {
        benchmark::DoNotOptimize(cbf.possibly_contains(non_member_keys[idx % n]));
        ++idx;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_CountingBloomLookupNegative)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kNanosecond);

static void BM_CountingBloomMemory(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    for (auto _ : state) {
        probds::CountingBloomFilter<std::uint64_t> cbf(n, 0.01);
        const double bytes = static_cast<double>(cbf.memory_bytes());
        const double uset_bytes = static_cast<double>(n * (sizeof(std::uint64_t) + 24.0));
        state.counters["cbf_bytes"] = bytes;
        state.counters["compression_ratio"] = uset_bytes / bytes;
    }
}
BENCHMARK(BM_CountingBloomMemory)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kMicrosecond);
