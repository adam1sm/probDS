// =============================================================================
// bloom_filter_bench.cpp — Benchmarks for probds::BloomFilter
// =============================================================================

#include "probds/bloom_filter.hpp"
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
// Bloom Filter Benchmarks
// =============================================================================

static void BM_BloomInsert(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto keys = generate_keys(n, 42);

    for (auto _ : state) {
        probds::BloomFilter<std::uint64_t> bf(n, 0.01);
        for (const auto& key : keys) {
            bf.insert(key);
        }
        benchmark::DoNotOptimize(bf);
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * static_cast<int64_t>(n));
}
BENCHMARK(BM_BloomInsert)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kMillisecond);

static void BM_BloomLookupPositive(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto keys = generate_keys(n, 42);

    probds::BloomFilter<std::uint64_t> bf(n, 0.01);
    for (const auto& key : keys) {
        bf.insert(key);
    }

    std::size_t idx = 0;
    for (auto _ : state) {
        benchmark::DoNotOptimize(bf.possibly_contains(keys[idx % n]));
        ++idx;
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}
BENCHMARK(BM_BloomLookupPositive)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kNanosecond);

static void BM_BloomLookupNegative(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto member_keys = generate_keys(n, 42);
    const auto non_member_keys = generate_keys(n, 43);

    probds::BloomFilter<std::uint64_t> bf(n, 0.01);
    for (const auto& key : member_keys) {
        bf.insert(key);
    }

    std::size_t idx = 0;
    for (auto _ : state) {
        benchmark::DoNotOptimize(bf.possibly_contains(non_member_keys[idx % n]));
        ++idx;
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}
BENCHMARK(BM_BloomLookupNegative)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kNanosecond);

// =============================================================================
// Baseline: std::unordered_set
// =============================================================================

static void BM_UnorderedSetInsert(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto keys = generate_keys(n, 42);

    for (auto _ : state) {
        std::unordered_set<std::uint64_t> set;
        set.reserve(n);
        for (const auto& key : keys) {
            set.insert(key);
        }
        benchmark::DoNotOptimize(set);
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * static_cast<int64_t>(n));
}
BENCHMARK(BM_UnorderedSetInsert)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kMillisecond);

static void BM_UnorderedSetLookupPositive(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto keys = generate_keys(n, 42);

    std::unordered_set<std::uint64_t> set(keys.begin(), keys.end());

    std::size_t idx = 0;
    for (auto _ : state) {
        benchmark::DoNotOptimize(set.count(keys[idx % n]));
        ++idx;
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}
BENCHMARK(BM_UnorderedSetLookupPositive)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kNanosecond);

static void BM_UnorderedSetLookupNegative(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto member_keys = generate_keys(n, 42);
    const auto non_member_keys = generate_keys(n, 43);

    std::unordered_set<std::uint64_t> set(member_keys.begin(), member_keys.end());

    std::size_t idx = 0;
    for (auto _ : state) {
        benchmark::DoNotOptimize(set.count(non_member_keys[idx % n]));
        ++idx;
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}
BENCHMARK(BM_UnorderedSetLookupNegative)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kNanosecond);

// =============================================================================
// Memory Usage Comparison
// =============================================================================

static void BM_BloomMemory(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));

    for (auto _ : state) {
        probds::BloomFilter<std::uint64_t> bf(n, 0.01);
        state.PauseTiming();
        state.counters["bloom_bytes"] = static_cast<double>(bf.memory_usage());
        const double uset_bytes = static_cast<double>(n) * (sizeof(std::uint64_t) + 24.0); // node size + overhead
        state.counters["uset_bytes_est"] = uset_bytes;
        state.counters["compression_ratio"] = uset_bytes / static_cast<double>(bf.memory_usage());
        state.ResumeTiming();
    }
}
BENCHMARK(BM_BloomMemory)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kMicrosecond);
