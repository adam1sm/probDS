// =============================================================================
// count_min_sketch_test.cpp — Tests for probds::CountMinSketch
// =============================================================================
//
// Test strategy:
//   1. Deterministic tests: verify invariants that must always hold
//      (overestimate-only, exact single-item counts, parameter formulas)
//   2. Statistical tests: verify probabilistic bounds hold within tolerance
//      (accuracy bound ε·N satisfied for ≥ (1-δ) fraction of queries)
//   3. Edge cases: invalid parameters, clear, multi-count inserts
//
// Statistical tests use generous margins (2-3×) to avoid flakiness while
// still catching gross implementation errors.
// =============================================================================

#include "probds/count_min_sketch.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

// =============================================================================
// Helpers
// =============================================================================

static std::string test_key(std::size_t i) {
    return "key_" + std::to_string(i);
}

// =============================================================================
// Test: OverestimateOnly
// The Count-Min Sketch can only overestimate, never underestimate.
// For every item, estimate(x) >= true_freq(x).
// =============================================================================
TEST(CountMinSketchTest, OverestimateOnly) {
    probds::CountMinSketch cms(0.01, 0.01);

    // Insert items with varying known frequencies
    std::unordered_map<std::string, std::uint64_t> true_freq;
    for (std::size_t i = 0; i < 1000; ++i) {
        const auto key = test_key(i);
        const std::uint64_t count = (i % 10) + 1;  // frequencies 1..10
        cms.insert(key, count);
        true_freq[key] = count;
    }

    // Every estimate must be >= the true frequency
    for (const auto& [key, freq] : true_freq) {
        ASSERT_GE(cms.estimate(key), freq)
            << "Underestimate detected for key=" << key
            << ", true=" << freq
            << ", estimate=" << cms.estimate(key);
    }
}

// =============================================================================
// Test: AccuracyBound
// Verify that the error is ≤ ε·N for at least (1-δ) fraction of queries.
//
// Setup: ε=0.01, δ=0.05, insert 10000 distinct items each with count 1.
// Then N = 10000, and the error bound is ε·N = 100.
// At least 95% of estimates should satisfy: estimate(x) ≤ 1 + 100 = 101.
//
// We use 2× the δ tolerance (allowing up to 10% violations) to avoid
// flakiness while still catching bugs.
// =============================================================================
TEST(CountMinSketchTest, AccuracyBound) {
    constexpr double epsilon = 0.01;
    constexpr double delta = 0.05;
    constexpr std::size_t n = 10000;

    probds::CountMinSketch cms(epsilon, delta);

    for (std::size_t i = 0; i < n; ++i) {
        cms.insert(test_key(i));
    }

    ASSERT_EQ(cms.total_count(), n);

    const double error_bound = epsilon * static_cast<double>(cms.total_count());
    std::size_t violations = 0;

    for (std::size_t i = 0; i < n; ++i) {
        const auto est = cms.estimate(test_key(i));
        // True frequency is 1; error = est - 1
        if (static_cast<double>(est) > 1.0 + error_bound) {
            ++violations;
        }
    }

    const double violation_rate =
        static_cast<double>(violations) / static_cast<double>(n);

    // At most δ fraction should violate. Use 2× tolerance for robustness.
    EXPECT_LE(violation_rate, delta * 2.0)
        << "Violation rate " << violation_rate
        << " exceeds 2x delta=" << delta
        << " (violations=" << violations << "/" << n << ")";
}

// =============================================================================
// Test: SingleItem
// Inserting a single item C times should give estimate == C exactly.
// With only one item, there are no collisions to inflate the counters
// beyond C, and the minimum across all rows must be exactly C.
// =============================================================================
TEST(CountMinSketchTest, SingleItem) {
    probds::CountMinSketch cms(0.01, 0.01);

    constexpr std::uint64_t count = 42;
    for (std::uint64_t i = 0; i < count; ++i) {
        cms.insert("only_item");
    }

    EXPECT_EQ(cms.estimate("only_item"), count);
    EXPECT_EQ(cms.total_count(), count);
}

// =============================================================================
// Test: ExactForFewItems
// With very few items inserted, the probability of collision across ALL
// d rows is negligible. Estimates should be exact or very close.
// =============================================================================
TEST(CountMinSketchTest, ExactForFewItems) {
    probds::CountMinSketch cms(0.001, 0.001);  // very wide sketch

    cms.insert("apple", 10);
    cms.insert("banana", 20);
    cms.insert("cherry", 30);

    // With width ≈ ⌈e/0.001⌉ = 2719, 3 items are extremely unlikely to
    // collide across all d rows. Estimates should be exact.
    EXPECT_EQ(cms.estimate("apple"), 10u);
    EXPECT_EQ(cms.estimate("banana"), 20u);
    EXPECT_EQ(cms.estimate("cherry"), 30u);
}

// =============================================================================
// Test: ZipfDistribution
// Simulate a realistic workload with Zipf-distributed frequencies.
// Items with rank r get frequency proportional to 1/r.
// The heavy hitters should have accurate estimates; the long tail may
// have larger relative error but should still satisfy the absolute bound.
// =============================================================================
TEST(CountMinSketchTest, ZipfDistribution) {
    constexpr double epsilon = 0.01;
    constexpr double delta = 0.01;
    constexpr std::size_t num_items = 1000;
    constexpr std::uint64_t max_freq = 10000;

    probds::CountMinSketch cms(epsilon, delta);
    std::unordered_map<std::string, std::uint64_t> true_freq;

    // Generate Zipf-like frequencies: freq(r) = max_freq / r
    for (std::size_t r = 1; r <= num_items; ++r) {
        const auto key = test_key(r);
        const std::uint64_t freq = max_freq / static_cast<std::uint64_t>(r);
        if (freq == 0) continue;
        cms.insert(key, freq);
        true_freq[key] = freq;
    }

    const double error_bound =
        epsilon * static_cast<double>(cms.total_count());

    // All estimates must be >= true frequency (overestimate-only)
    for (const auto& [key, freq] : true_freq) {
        const auto est = cms.estimate(key);
        ASSERT_GE(est, freq)
            << "Underestimate for " << key;
    }

    // Count violations of the ε·N bound
    std::size_t violations = 0;
    for (const auto& [key, freq] : true_freq) {
        const auto est = cms.estimate(key);
        if (static_cast<double>(est - freq) > error_bound) {
            ++violations;
        }
    }

    const double violation_rate =
        static_cast<double>(violations) / static_cast<double>(true_freq.size());

    EXPECT_LE(violation_rate, delta * 3.0)
        << "Zipf violation rate " << violation_rate
        << " exceeds 3x delta=" << delta;
}

// =============================================================================
// Test: Parameters
// Verify that computed width and depth match the theoretical formulas.
//   w = ⌈e/ε⌉
//   d = ⌈ln(1/δ)⌉
// =============================================================================
TEST(CountMinSketchTest, Parameters) {
    auto next_pow2 = [](std::size_t n) {
        if (n == 0) return std::size_t{1};
        --n;
        n |= n >> 1;
        n |= n >> 2;
        n |= n >> 4;
        n |= n >> 8;
        n |= n >> 16;
        n |= n >> 32;
        return n + 1;
    };
    {
        constexpr double epsilon = 0.01;
        constexpr double delta = 0.01;
        probds::CountMinSketch cms(epsilon, delta);

        const auto expected_w = static_cast<std::size_t>(
            std::ceil(std::exp(1.0) / epsilon));
        const auto expected_d = static_cast<std::size_t>(
            std::ceil(std::log(1.0 / delta)));

        EXPECT_EQ(cms.width(), next_pow2(expected_w));   // 272 rounded up to 512
        EXPECT_EQ(cms.depth(), expected_d);    // ⌈ln(100)⌉ = 5
    }
    {
        constexpr double epsilon = 0.001;
        constexpr double delta = 0.05;
        probds::CountMinSketch cms(epsilon, delta);

        const auto expected_w = static_cast<std::size_t>(
            std::ceil(std::exp(1.0) / epsilon));
        const auto expected_d = static_cast<std::size_t>(
            std::ceil(std::log(1.0 / delta)));

        EXPECT_EQ(cms.width(), next_pow2(expected_w));   // 2719 rounded up to 4096
        EXPECT_EQ(cms.depth(), expected_d);    // ⌈ln(20)⌉ = 3
    }
}

// =============================================================================
// Test: TotalCount
// Verify total_count() tracks the sum of all insertions accurately.
// =============================================================================
TEST(CountMinSketchTest, TotalCount) {
    probds::CountMinSketch cms(0.01, 0.01);

    EXPECT_EQ(cms.total_count(), 0u);

    cms.insert("a", 10);
    EXPECT_EQ(cms.total_count(), 10u);

    cms.insert("b", 20);
    EXPECT_EQ(cms.total_count(), 30u);

    cms.insert("a", 5);
    EXPECT_EQ(cms.total_count(), 35u);

    // Insert with default count = 1
    cms.insert("c");
    EXPECT_EQ(cms.total_count(), 36u);
}

// =============================================================================
// Test: Clear
// After clear(), all estimates are 0 and total_count is 0.
// =============================================================================
TEST(CountMinSketchTest, Clear) {
    probds::CountMinSketch cms(0.01, 0.01);

    // Insert some data
    for (std::size_t i = 0; i < 500; ++i) {
        cms.insert(test_key(i), 10);
    }
    ASSERT_GT(cms.total_count(), 0u);

    cms.clear();

    EXPECT_EQ(cms.total_count(), 0u);

    // All estimates should be 0 after clear
    for (std::size_t i = 0; i < 500; ++i) {
        EXPECT_EQ(cms.estimate(test_key(i)), 0u)
            << "Non-zero estimate after clear for " << test_key(i);
    }

    // Should be able to reuse after clear
    cms.insert("fresh_key", 7);
    EXPECT_EQ(cms.total_count(), 7u);
    EXPECT_GE(cms.estimate("fresh_key"), 7u);
}

// =============================================================================
// Test: MultipleInserts
// insert(key, 5) should be equivalent to inserting key 5 times.
// =============================================================================
TEST(CountMinSketchTest, MultipleInserts) {
    probds::CountMinSketch cms_batch(0.01, 0.01);
    probds::CountMinSketch cms_single(0.01, 0.01);

    // Batch insert: insert key with count=5
    cms_batch.insert("test_item", 5);

    // Single inserts: insert key 5 times
    for (int i = 0; i < 5; ++i) {
        cms_single.insert("test_item");
    }

    EXPECT_EQ(cms_batch.estimate("test_item"),
              cms_single.estimate("test_item"));
    EXPECT_EQ(cms_batch.total_count(), cms_single.total_count());
}

// =============================================================================
// Test: InvalidParameters
// epsilon=0, delta=0, or values outside (0,1) should throw.
// =============================================================================
TEST(CountMinSketchTest, InvalidParameters) {
    // epsilon out of range
    EXPECT_THROW(probds::CountMinSketch(0.0, 0.01), std::invalid_argument);
    EXPECT_THROW(probds::CountMinSketch(1.0, 0.01), std::invalid_argument);
    EXPECT_THROW(probds::CountMinSketch(-0.5, 0.01), std::invalid_argument);

    // delta out of range
    EXPECT_THROW(probds::CountMinSketch(0.01, 0.0), std::invalid_argument);
    EXPECT_THROW(probds::CountMinSketch(0.01, 1.0), std::invalid_argument);
    EXPECT_THROW(probds::CountMinSketch(0.01, -0.1), std::invalid_argument);

    // Both out of range
    EXPECT_THROW(probds::CountMinSketch(0.0, 0.0), std::invalid_argument);
}

// =============================================================================
// Test: MemoryUsage
// Verify memory_usage() ≈ width * depth * 8 bytes.
// =============================================================================
TEST(CountMinSketchTest, MemoryUsage) {
    constexpr double epsilon = 0.01;
    constexpr double delta = 0.01;
    probds::CountMinSketch cms(epsilon, delta);

    const std::size_t expected_bytes =
        cms.width() * cms.depth() * sizeof(std::uint64_t);

    EXPECT_EQ(cms.memory_usage(), expected_bytes);

    // Sanity check: memory should be reasonable
    EXPECT_GT(cms.memory_usage(), 0u);
    EXPECT_LT(cms.memory_usage(), 100 * 1024 * 1024);  // < 100 MB
}

// =============================================================================
// Test: UnseenItems
// Items never inserted should have estimate 0 in a fresh sketch (no
// collisions if nothing has been inserted).
// =============================================================================
TEST(CountMinSketchTest, UnseenItems) {
    probds::CountMinSketch cms(0.01, 0.01);

    for (std::size_t i = 0; i < 100; ++i) {
        EXPECT_EQ(cms.estimate(test_key(i)), 0u);
    }
}

// =============================================================================
// Test: HighFrequencyItem
// A single very high-frequency item among many low-frequency items
// should be estimated accurately.
// =============================================================================
TEST(CountMinSketchTest, HighFrequencyItem) {
    constexpr double epsilon = 0.01;
    constexpr double delta = 0.01;
    probds::CountMinSketch cms(epsilon, delta);

    // Insert one heavy hitter
    constexpr std::uint64_t heavy_count = 100000;
    cms.insert("heavy_hitter", heavy_count);

    // Insert many light items
    for (std::size_t i = 0; i < 1000; ++i) {
        cms.insert(test_key(i));
    }

    const auto est = cms.estimate("heavy_hitter");
    const double error_bound =
        epsilon * static_cast<double>(cms.total_count());

    // Estimate should be >= true count (overestimate-only)
    EXPECT_GE(est, heavy_count);

    // Error should be within ε·N
    EXPECT_LE(static_cast<double>(est - heavy_count), error_bound)
        << "Heavy hitter error " << (est - heavy_count)
        << " exceeds bound " << error_bound;
}

// =============================================================================
// Test: DeterministicSeeds
// Two sketches with the same parameters should produce identical estimates.
// This verifies that seed generation is deterministic.
// =============================================================================
TEST(CountMinSketchTest, DeterministicSeeds) {
    probds::CountMinSketch cms1(0.01, 0.01);
    probds::CountMinSketch cms2(0.01, 0.01);

    for (std::size_t i = 0; i < 100; ++i) {
        const auto key = test_key(i);
        cms1.insert(key, i + 1);
        cms2.insert(key, i + 1);
    }

    for (std::size_t i = 0; i < 100; ++i) {
        EXPECT_EQ(cms1.estimate(test_key(i)), cms2.estimate(test_key(i)));
    }
}
