// =============================================================================
// cuckoo_filter_bench.cpp — Benchmarks for probds::CuckooFilter
// =============================================================================

#include "probds/cuckoo_filter.hpp"
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
// Cuckoo Filter Benchmarks
// =============================================================================

static void BM_CuckooInsert(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto keys = generate_keys(n, 42);

    for (auto _ : state) {
        probds::CuckooFilter<std::uint64_t> cf(n * 2);
        for (const auto& key : keys) {
            benchmark::DoNotOptimize(cf.insert(key));
        }
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * static_cast<int64_t>(n));
}
BENCHMARK(BM_CuckooInsert)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kMillisecond);

static void BM_CuckooLookupPositive(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto keys = generate_keys(n, 42);

    probds::CuckooFilter<std::uint64_t> cf(n * 2);
    for (const auto& key : keys) {
        cf.insert(key);
    }

    std::size_t idx = 0;
    for (auto _ : state) {
        benchmark::DoNotOptimize(cf.possibly_contains(keys[idx % n]));
        ++idx;
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}
BENCHMARK(BM_CuckooLookupPositive)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kNanosecond);

static void BM_CuckooLookupNegative(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto member_keys = generate_keys(n, 42);
    const auto non_member_keys = generate_keys(n, 43);

    probds::CuckooFilter<std::uint64_t> cf(n * 2);
    for (const auto& key : member_keys) {
        cf.insert(key);
    }

    std::size_t idx = 0;
    for (auto _ : state) {
        benchmark::DoNotOptimize(cf.possibly_contains(non_member_keys[idx % n]));
        ++idx;
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}
BENCHMARK(BM_CuckooLookupNegative)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kNanosecond);

static void BM_CuckooDelete(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto keys = generate_keys(n, 42);

    for (auto _ : state) {
        state.PauseTiming();
        probds::CuckooFilter<std::uint64_t> cf(n * 2);
        for (const auto& key : keys) {
            cf.insert(key);
        }
        state.ResumeTiming();

        for (const auto& key : keys) {
            benchmark::DoNotOptimize(cf.remove(key));
        }
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * static_cast<int64_t>(n));
}
BENCHMARK(BM_CuckooDelete)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kMillisecond);

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

static void BM_UnorderedSetLookup(benchmark::State& state) {
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
BENCHMARK(BM_UnorderedSetLookup)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kNanosecond);

// =============================================================================
// Memory Usage Comparison
// =============================================================================

static void BM_CuckooMemory(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));

    for (auto _ : state) {
        probds::CuckooFilter<std::uint64_t> cf(n * 2);
        benchmark::DoNotOptimize(cf);

        state.PauseTiming();
        state.counters["cuckoo_bytes"] = static_cast<double>(cf.memory_usage());
        const double uset_bytes = static_cast<double>(n) * (sizeof(std::uint64_t) + 24.0);
        state.counters["uset_bytes_est"] = uset_bytes;
        state.counters["compression_ratio"] = uset_bytes / static_cast<double>(cf.memory_usage());
        state.ResumeTiming();
    }
}
BENCHMARK(BM_CuckooMemory)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kMicrosecond);
