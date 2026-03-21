// =============================================================================
// count_min_sketch_bench.cpp — Benchmarks for probds::CountMinSketch
// =============================================================================

#include "probds/count_min_sketch.hpp"
#include <benchmark/benchmark.h>
#include <random>
#include <unordered_map>
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
// Count-Min Sketch Benchmarks
// =============================================================================

static void BM_CMSInsert(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto keys = generate_keys(n, 42);

    for (auto _ : state) {
        probds::CountMinSketch<std::uint64_t> cms(0.01, 0.01);
        for (const auto& key : keys) {
            cms.insert(key);
        }
        benchmark::DoNotOptimize(cms);
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * static_cast<int64_t>(n));
}
BENCHMARK(BM_CMSInsert)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kMillisecond);

static void BM_CMSEstimate(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto keys = generate_keys(n, 42);

    probds::CountMinSketch<std::uint64_t> cms(0.01, 0.01);
    for (const auto& key : keys) {
        cms.insert(key);
    }

    std::size_t idx = 0;
    for (auto _ : state) {
        benchmark::DoNotOptimize(cms.estimate(keys[idx % n]));
        ++idx;
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}
BENCHMARK(BM_CMSEstimate)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kNanosecond);

// =============================================================================
// Baseline: std::unordered_map
// =============================================================================

static void BM_UnorderedMapInsert(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto keys = generate_keys(n, 42);

    for (auto _ : state) {
        std::unordered_map<std::uint64_t, std::uint64_t> map;
        map.reserve(n);
        for (const auto& key : keys) {
            map[key] += 1;
        }
        benchmark::DoNotOptimize(map);
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * static_cast<int64_t>(n));
}
BENCHMARK(BM_UnorderedMapInsert)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kMillisecond);

static void BM_UnorderedMapLookup(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto keys = generate_keys(n, 42);

    std::unordered_map<std::uint64_t, std::uint64_t> map;
    map.reserve(n);
    for (const auto& key : keys) {
        map[key] += 1;
    }

    std::size_t idx = 0;
    for (auto _ : state) {
        auto it = map.find(keys[idx % n]);
        benchmark::DoNotOptimize(it->second);
        ++idx;
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}
BENCHMARK(BM_UnorderedMapLookup)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kNanosecond);

// =============================================================================
// Memory Usage Comparison
// =============================================================================

static void BM_CMSMemory(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));

    for (auto _ : state) {
        probds::CountMinSketch<std::uint64_t> cms(0.01, 0.01);
        state.PauseTiming();
        state.counters["cms_bytes"] = static_cast<double>(cms.memory_usage());
        const double umap_bytes = static_cast<double>(n) * (sizeof(std::uint64_t) + sizeof(std::uint64_t) + 24.0);
        state.counters["umap_bytes_est"] = umap_bytes;
        state.counters["compression_ratio"] = umap_bytes / static_cast<double>(cms.memory_usage());
        state.ResumeTiming();
    }
}
BENCHMARK(BM_CMSMemory)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kMicrosecond);
