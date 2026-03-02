// =============================================================================
// ribbon_filter_bench.cpp — Benchmarks for probds::RibbonFilter
// =============================================================================

#include "probds/ribbon_filter.hpp"
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
// RibbonFilter Benchmarks
// =============================================================================

static void BM_RibbonFilterInsert(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto keys = generate_keys(n, 42);

    for (auto _ : state) {
        probds::RibbonFilter<std::uint64_t, std::uint8_t, std::uint64_t> filter(keys.begin(), keys.end());
        benchmark::DoNotOptimize(filter);
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(BM_RibbonFilterInsert)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kMillisecond);

static void BM_RibbonFilterLookupPositive(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto keys = generate_keys(n, 42);

    probds::RibbonFilter<std::uint64_t, std::uint8_t, std::uint64_t> filter(keys.begin(), keys.end());

    std::size_t idx = 0;
    for (auto _ : state) {
        benchmark::DoNotOptimize(filter.possibly_contains(keys[idx % n]));
        ++idx;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_RibbonFilterLookupPositive)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kNanosecond);

static void BM_RibbonFilterLookupNegative(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto member_keys = generate_keys(n, 42);
    const auto non_member_keys = generate_keys(n, 43);

    probds::RibbonFilter<std::uint64_t, std::uint8_t, std::uint64_t> filter(member_keys.begin(), member_keys.end());

    std::size_t idx = 0;
    for (auto _ : state) {
        benchmark::DoNotOptimize(filter.possibly_contains(non_member_keys[idx % n]));
        ++idx;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_RibbonFilterLookupNegative)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kNanosecond);

static void BM_RibbonFilterMemory(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    
    for (auto _ : state) {
        state.PauseTiming();
        std::vector<std::uint64_t> keys = {0, 1}; // dummy size
        probds::RibbonFilter<std::uint64_t, std::uint8_t, std::uint64_t> filter(keys.begin(), keys.end());
        
        // Compute size as if populated with n keys
        double size_factor = 1.08;
        std::uint32_t num_starts = 32;
        while (num_starts + 64 < size_factor * n) {
            num_starts <<= 1;
        }
        std::uint32_t num_slots = num_starts + 64;
        
        state.counters["ribbon_bytes"] = static_cast<double>(num_slots * sizeof(std::uint8_t) + sizeof(probds::RibbonFilter<std::uint64_t, std::uint8_t, std::uint64_t>));
        state.ResumeTiming();
    }
}
BENCHMARK(BM_RibbonFilterMemory)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kMicrosecond);
