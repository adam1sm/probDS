// =============================================================================
// quotient_filter_bench.cpp — Benchmarks for probds::QuotientFilter
// =============================================================================

#include "probds/quotient_filter.hpp"
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
// Quotient Filter Benchmarks
// =============================================================================

static void BM_QuotientInsert(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto keys = generate_keys(n, 42);

    for (auto _ : state) {
        probds::QuotientFilter<std::uint64_t> qf(n);
        for (const auto& key : keys) {
            qf.insert(key);
        }
        benchmark::DoNotOptimize(qf);
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(BM_QuotientInsert)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kMillisecond);

static void BM_QuotientLookupPositive(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto keys = generate_keys(n, 42);

    probds::QuotientFilter<std::uint64_t> qf(n);
    for (const auto& key : keys) {
        qf.insert(key);
    }

    for (auto _ : state) {
        for (const auto& key : keys) {
            bool res = qf.possibly_contains(key);
            benchmark::DoNotOptimize(res);
        }
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(BM_QuotientLookupPositive)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kMillisecond);

static void BM_QuotientLookupNegative(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto member_keys = generate_keys(n, 42);
    const auto non_member_keys = generate_keys(n, 43);

    probds::QuotientFilter<std::uint64_t> qf(n);
    for (const auto& key : member_keys) {
        qf.insert(key);
    }

    for (auto _ : state) {
        for (const auto& key : non_member_keys) {
            bool res = qf.possibly_contains(key);
            benchmark::DoNotOptimize(res);
        }
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(BM_QuotientLookupNegative)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kMillisecond);

static void BM_QuotientDelete(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto keys = generate_keys(n, 42);

    for (auto _ : state) {
        probds::QuotientFilter<std::uint64_t> qf(n);
        for (const auto& key : keys) {
            qf.insert(key);
        }
        for (const auto& key : keys) {
            qf.remove(key);
        }
        benchmark::DoNotOptimize(qf);
    }
    state.SetItemsProcessed(state.iterations() * n * 2);
}
BENCHMARK(BM_QuotientDelete)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kMillisecond);
