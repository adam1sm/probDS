// =============================================================================
// hyperloglog_bench.cpp — Benchmarks for probds::HyperLogLog
// =============================================================================

#include "probds/hyperloglog.hpp"
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
// HyperLogLog Benchmarks
// =============================================================================

static void BM_HLLInsert(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto keys = generate_keys(n, 42);

    for (auto _ : state) {
        probds::HyperLogLog<std::uint64_t> hll(14);
        for (const auto& key : keys) {
            hll.insert(key);
        }
        benchmark::DoNotOptimize(hll);
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * static_cast<int64_t>(n));
}
BENCHMARK(BM_HLLInsert)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kMillisecond);

static void BM_HLLEstimate(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto keys = generate_keys(n, 42);

    probds::HyperLogLog<std::uint64_t> hll(14);
    for (const auto& key : keys) {
        hll.insert(key);
    }

    for (auto _ : state) {
        benchmark::DoNotOptimize(hll.estimate());
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}
BENCHMARK(BM_HLLEstimate)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kMicrosecond);

static void BM_HLLMerge(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto keys_a = generate_keys(n, 42);
    const auto keys_b = generate_keys(n, 43);

    probds::HyperLogLog<std::uint64_t> hll_a(14);
    probds::HyperLogLog<std::uint64_t> hll_b(14);
    for (const auto& key : keys_a) {
        hll_a.insert(key);
    }
    for (const auto& key : keys_b) {
        hll_b.insert(key);
    }

    for (auto _ : state) {
        state.PauseTiming();
        probds::HyperLogLog<std::uint64_t> merged = hll_a;
        state.ResumeTiming();
        merged.merge(hll_b);
        benchmark::DoNotOptimize(merged);
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}
BENCHMARK(BM_HLLMerge)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kMicrosecond);

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
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * static_cast<int64_t>(n));
}
BENCHMARK(BM_UnorderedSetInsert)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kMillisecond);

static void BM_UnorderedSetSize(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto keys = generate_keys(n, 42);

    std::unordered_set<std::uint64_t> set(keys.begin(), keys.end());

    for (auto _ : state) {
        benchmark::DoNotOptimize(set.size());
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}
BENCHMARK(BM_UnorderedSetSize)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kMicrosecond);

// =============================================================================
// Memory Usage Comparison
// =============================================================================

static void BM_HLLMemory(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));

    for (auto _ : state) {
        probds::HyperLogLog<std::uint64_t> hll(14);
        state.PauseTiming();
        state.counters["hll_bytes"] = static_cast<double>(hll.memory_usage());
        const double uset_bytes = static_cast<double>(n) * (sizeof(std::uint64_t) + 24.0);
        state.counters["uset_bytes_est"] = uset_bytes;
        state.counters["compression_ratio"] = uset_bytes / static_cast<double>(hll.memory_usage());
        state.ResumeTiming();
    }
}
BENCHMARK(BM_HLLMemory)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kMicrosecond);
