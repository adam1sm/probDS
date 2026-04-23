// =============================================================================
// kll_sketch_bench.cpp — Benchmarks for probds::KLLSketch
// =============================================================================

#include "probds/kll_sketch.hpp"
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
// KLL Sketch Benchmarks
// =============================================================================

static void BM_KLLSketchInsert(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto data = generate_data(n);
    std::uint64_t total_compactions = 0;

    for (auto _ : state) {
        probds::KLLSketch kll(200);
        for (double val : data) {
            kll.insert(val);
        }
        total_compactions += kll.compaction_count();
        benchmark::DoNotOptimize(kll);
    }
    state.SetItemsProcessed(state.iterations() * n);
    
    const double total_inserts = static_cast<double>(state.iterations()) * static_cast<double>(n);
    state.counters["compactions_per_10k"] = benchmark::Counter(
        static_cast<double>(total_compactions) / (total_inserts / 10000.0),
        benchmark::Counter::kAvgThreads
    );
}
BENCHMARK(BM_KLLSketchInsert)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kMillisecond);

static void BM_KLLSketchQuantile(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto data = generate_data(n);
    probds::KLLSketch kll(200);
    for (double val : data) {
        kll.insert(val);
    }

    for (auto _ : state) {
        double q = kll.get_quantile(0.5);
        benchmark::DoNotOptimize(q);
    }
}
BENCHMARK(BM_KLLSketchQuantile)
    ->Arg(10000)
    ->Arg(1000000)
    ->Arg(50000000)
    ->Unit(benchmark::kNanosecond);
