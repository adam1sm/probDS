// =============================================================================
// blocked_bloom_filter_bench.cpp — Benchmarks for probds::BlockedBloomFilter
// =============================================================================

#include "probds/blocked_bloom_filter.hpp"
#include <benchmark/benchmark.h>
#include <array>
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
// Blocked Bloom Filter Benchmarks
// =============================================================================

static void BM_BlockedBloomInsert(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto keys = generate_keys(n, 42);

    for (auto _ : state) {
        probds::BlockedBloomFilter<std::uint64_t> bbf(n, 0.01);
        for (const auto& key : keys) {
            bbf.insert(key);
        }
        benchmark::DoNotOptimize(bbf);
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(BM_BlockedBloomInsert)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kMillisecond);

static void BM_BlockedBloomLookupScalar(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto keys = generate_keys(n, 42);

    probds::BlockedBloomFilter<std::uint64_t> bbf(n, 0.01);
    for (const auto& key : keys) {
        bbf.insert(key);
    }

    for (auto _ : state) {
        for (const auto& key : keys) {
            bool res = bbf.possibly_contains(key);
            benchmark::DoNotOptimize(res);
        }
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(BM_BlockedBloomLookupScalar)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kMillisecond);

static void BM_BlockedBloomLookupBulk(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto keys = generate_keys(n, 42);

    probds::BlockedBloomFilter<std::uint64_t> bbf(n, 0.01);
    for (const auto& key : keys) {
        bbf.insert(key);
    }

    std::vector<uint8_t> results(n);

    for (auto _ : state) {
        bbf.possibly_contains_bulk(keys.data(), reinterpret_cast<bool*>(results.data()), n);
        benchmark::DoNotOptimize(results);
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(BM_BlockedBloomLookupBulk)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kMillisecond);

template <std::size_t BatchSize>
static void BM_BlockedBloomLookupBatch(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto keys = generate_keys(n, 42);

    probds::BlockedBloomFilter<std::uint64_t> bbf(n, 0.01);
    for (const auto& key : keys) {
        bbf.insert(key);
    }

    for (auto _ : state) {
        for (std::size_t i = 0; i + BatchSize - 1 < n; i += BatchSize) {
            std::array<const std::uint64_t*, BatchSize> batch_keys;
            for (std::size_t j = 0; j < BatchSize; ++j) {
                batch_keys[j] = &keys[i + j];
            }
            auto res = bbf.template possibly_contains_batch<BatchSize>(batch_keys);
            benchmark::DoNotOptimize(res);
        }
    }
    state.SetItemsProcessed(state.iterations() * (n - (n % BatchSize)));
}

BENCHMARK_TEMPLATE(BM_BlockedBloomLookupBatch, 1)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kMillisecond);

BENCHMARK_TEMPLATE(BM_BlockedBloomLookupBatch, 4)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kMillisecond);

BENCHMARK_TEMPLATE(BM_BlockedBloomLookupBatch, 8)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kMillisecond);

BENCHMARK_TEMPLATE(BM_BlockedBloomLookupBatch, 16)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kMillisecond);


