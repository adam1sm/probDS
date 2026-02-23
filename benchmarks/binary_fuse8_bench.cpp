// =============================================================================
// binary_fuse8_bench.cpp — Benchmarks for probds::BinaryFuse8
// =============================================================================

#include "probds/binary_fuse8.hpp"
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
// BinaryFuse8 Benchmarks
// =============================================================================

static void BM_BinaryFuse8Insert(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto keys = generate_keys(n, 42);

    for (auto _ : state) {
        probds::BinaryFuse8<std::uint64_t> filter(keys.begin(), keys.end());
        benchmark::DoNotOptimize(filter);
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(BM_BinaryFuse8Insert)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kMillisecond);

static void BM_BinaryFuse8LookupPositive(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto keys = generate_keys(n, 42);

    probds::BinaryFuse8<std::uint64_t> filter(keys.begin(), keys.end());

    std::size_t idx = 0;
    for (auto _ : state) {
        benchmark::DoNotOptimize(filter.possibly_contains(keys[idx % n]));
        ++idx;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_BinaryFuse8LookupPositive)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kNanosecond);

static void BM_BinaryFuse8LookupNegative(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto member_keys = generate_keys(n, 42);
    const auto non_member_keys = generate_keys(n, 43);

    probds::BinaryFuse8<std::uint64_t> filter(member_keys.begin(), member_keys.end());

    std::size_t idx = 0;
    for (auto _ : state) {
        benchmark::DoNotOptimize(filter.possibly_contains(non_member_keys[idx % n]));
        ++idx;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_BinaryFuse8LookupNegative)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kNanosecond);

static void BM_BinaryFuse8Memory(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    
    for (auto _ : state) {
        state.PauseTiming();
        std::vector<std::uint64_t> keys = {0, 1}; // dummy size
        probds::BinaryFuse8<std::uint64_t> filter(keys.begin(), keys.end());
        
        // Manually compute size as if populated with n keys
        // binary fuse capacity size factor is roughly ~1.125
        std::uint32_t segment_len = std::uint32_t{1} << static_cast<unsigned>(std::floor(std::log(static_cast<double>(n)) / std::log(3.33) + 2.25));
        if (segment_len > 262144) segment_len = 262144;
        double factor = std::max(1.125, 0.875 + 0.25 * std::log(1000000.0) / std::log(static_cast<double>(n)));
        std::uint32_t capacity = static_cast<std::uint32_t>(std::round(static_cast<double>(n) * factor));
        std::uint32_t initSegmentCount = (capacity + segment_len - 1) / segment_len - 2;
        std::uint32_t array_len = (initSegmentCount + 2) * segment_len;
        
        state.counters["fuse_bytes"] = static_cast<double>(array_len * sizeof(std::uint8_t) + sizeof(probds::BinaryFuse8<std::uint64_t>));
        state.ResumeTiming();
    }
}
BENCHMARK(BM_BinaryFuse8Memory)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kMicrosecond);
