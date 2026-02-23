// =============================================================================
// xor_filter_bench.cpp — Benchmarks for probds::XorFilter
// =============================================================================

#include "probds/xor_filter.hpp"
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
// XorFilter Benchmarks
// =============================================================================

static void BM_XorFilterInsert(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto keys = generate_keys(n, 42);

    for (auto _ : state) {
        probds::XorFilter<std::uint64_t, std::uint8_t> filter(keys.begin(), keys.end());
        benchmark::DoNotOptimize(filter);
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(BM_XorFilterInsert)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kMillisecond);

static void BM_XorFilterLookupPositive(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto keys = generate_keys(n, 42);

    probds::XorFilter<std::uint64_t, std::uint8_t> filter(keys.begin(), keys.end());

    std::size_t idx = 0;
    for (auto _ : state) {
        benchmark::DoNotOptimize(filter.possibly_contains(keys[idx % n]));
        ++idx;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_XorFilterLookupPositive)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kNanosecond);

static void BM_XorFilterLookupNegative(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto member_keys = generate_keys(n, 42);
    const auto non_member_keys = generate_keys(n, 43);

    probds::XorFilter<std::uint64_t, std::uint8_t> filter(member_keys.begin(), member_keys.end());

    std::size_t idx = 0;
    for (auto _ : state) {
        benchmark::DoNotOptimize(filter.possibly_contains(non_member_keys[idx % n]));
        ++idx;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_XorFilterLookupNegative)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kNanosecond);

static void BM_XorFilterMemory(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    
    for (auto _ : state) {
        state.PauseTiming();
        std::vector<std::uint64_t> keys = {0, 1}; // dummy size
        probds::XorFilter<std::uint64_t, std::uint8_t> filter(keys.begin(), keys.end());
        
        // Compute size as if populated with n keys
        std::uint32_t block_len = 1;
        while (3 * block_len < 1.23 * n) {
            block_len <<= 1;
        }
        
        state.counters["xor_bytes"] = static_cast<double>(3 * block_len * sizeof(std::uint8_t) + sizeof(probds::XorFilter<std::uint64_t, std::uint8_t>));
        state.ResumeTiming();
    }
}
BENCHMARK(BM_XorFilterMemory)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kMicrosecond);
