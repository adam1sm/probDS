// =============================================================================
// exponential_histogram_bench.cpp — Benchmarks for probds::ExponentialHistogram
// =============================================================================

#include "probds/exponential_histogram.hpp"
#include <benchmark/benchmark.h>
#include <random>
#include <vector>

// =============================================================================
// ExponentialHistogram Benchmarks
// =============================================================================

static void BM_ExpHistUpdate(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    probds::ExponentialHistogram eh(n, 0.1);

    std::size_t idx = 0;
    for (auto _ : state) {
        eh.update(idx % 3 == 0);
        ++idx;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_ExpHistUpdate)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kNanosecond);

static void BM_ExpHistEstimate(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    probds::ExponentialHistogram eh(n, 0.1);
    for (std::size_t i = 0; i < std::min(n, std::size_t{100000}); ++i) {
        eh.update(i % 3 == 0);
    }

    std::size_t idx = 0;
    for (auto _ : state) {
        benchmark::DoNotOptimize(eh.estimate_last_n(n / 2));
        ++idx;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_ExpHistEstimate)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kNanosecond);
