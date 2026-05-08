// =============================================================================
// concurrent_bench.cpp — Benchmarks for concurrent structures
// =============================================================================

#include "probds/concurrent.hpp"
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

// Global instances for multithreaded sharing
static std::unique_ptr<probds::ConcurrentBloomFilter<std::uint64_t>> g_cbf;
static std::unique_ptr<probds::ConcurrentHyperLogLog<std::uint64_t>> g_chll;
static std::unique_ptr<probds::ConcurrentCountMin<std::uint64_t>> g_ccm;

static void SetupGlobalStructures(const benchmark::State&) {
    g_cbf = std::make_unique<probds::ConcurrentBloomFilter<std::uint64_t>>(10000000);
    g_chll = std::make_unique<probds::ConcurrentHyperLogLog<std::uint64_t>>(14);
    g_ccm = std::make_unique<probds::ConcurrentCountMin<std::uint64_t>>(0.01, 0.01);
}

// =============================================================================
// Concurrent Bloom Filter Benchmarks
// =============================================================================

static void BM_ConcurrentBloomFilterInsert(benchmark::State& state) {
    if (state.thread_index() == 0) {
        SetupGlobalStructures(state);
    }

    const auto keys = generate_keys(10000, 42 + state.thread_index());

    for (auto _ : state) {
        for (const auto& key : keys) {
            g_cbf->insert(key);
        }
    }
    state.SetItemsProcessed(state.iterations() * keys.size());
}
BENCHMARK(BM_ConcurrentBloomFilterInsert)
    ->ThreadRange(1, 8)
    ->Unit(benchmark::kMillisecond);

// =============================================================================
// Concurrent HyperLogLog Benchmarks
// =============================================================================

static void BM_ConcurrentHyperLogLogInsert(benchmark::State& state) {
    if (state.thread_index() == 0) {
        SetupGlobalStructures(state);
    }

    const auto keys = generate_keys(10000, 42 + state.thread_index());

    for (auto _ : state) {
        for (const auto& key : keys) {
            g_chll->insert(key);
        }
    }
    state.SetItemsProcessed(state.iterations() * keys.size());
}
BENCHMARK(BM_ConcurrentHyperLogLogInsert)
    ->ThreadRange(1, 8)
    ->Unit(benchmark::kMillisecond);

// =============================================================================
// Concurrent Count-Min Benchmarks
// =============================================================================

static void BM_ConcurrentCountMinInsert(benchmark::State& state) {
    if (state.thread_index() == 0) {
        SetupGlobalStructures(state);
    }

    const auto keys = generate_keys(10000, 42 + state.thread_index());

    for (auto _ : state) {
        for (const auto& key : keys) {
            g_ccm->insert(key, 1);
        }
    }
    state.SetItemsProcessed(state.iterations() * keys.size());
}
BENCHMARK(BM_ConcurrentCountMinInsert)
    ->ThreadRange(1, 8)
    ->Unit(benchmark::kMillisecond);
