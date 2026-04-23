// =============================================================================
// tdigest_bench.cpp — Benchmarks for probds::tDigest
// =============================================================================

#include "probds/tdigest.hpp"
#include <benchmark/benchmark.h>
#include <random>
#include <vector>

// =============================================================================
// Helpers
// =============================================================================

static std::vector<double> generate_data(std::size_t n) {
    std::vector<double> data(n);
    std::mt19937 gen(42);
    std::normal_distribution<double> dis(100.0, 15.0);
    for (std::size_t i = 0; i < n; ++i) {
        data[i] = dis(gen);
    }
    return data;
}

// =============================================================================
// t-Digest Benchmarks
// =============================================================================

static void BM_tDigestInsert(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto data = generate_data(n);

    for (auto _ : state) {
        probds::tDigest td(100.0);
        for (double val : data) {
            td.insert(val);
        }
        benchmark::DoNotOptimize(td);
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(BM_tDigestInsert)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kMillisecond);

static void BM_tDigestQuantile(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto data = generate_data(n);
    probds::tDigest td(100.0);
    for (double val : data) {
        td.insert(val);
    }
    td.compress();

    for (auto _ : state) {
        double q = td.quantile(0.5);
        benchmark::DoNotOptimize(q);
    }
}
BENCHMARK(BM_tDigestQuantile)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kNanosecond);
