// =============================================================================
// hyperloglog_test.cpp — Tests for probds::HyperLogLog
// =============================================================================
//
// Test strategy:
//   1. Deterministic tests: verify invariants that must always hold
//      (empty estimate, clear, precision, memory usage, error handling)
//   2. Statistical tests: verify cardinality estimates are within bounded
//      error (3σ tolerance = 99.7% confidence interval)
//
// For accuracy tests, we use the bound:
//   |estimate - actual| / actual < 3 * relative_error()
//
// where relative_error() = 1.04 / sqrt(2^p). The 3σ bound gives 99.7%
// confidence that the test passes even with random hash behavior.
// =============================================================================

#include "probds/hyperloglog.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <unordered_set>

// =============================================================================
// Helpers
// =============================================================================

static std::string test_key(std::size_t i) {
    return "hll_item_" + std::to_string(i);
}



/// Check that the HLL estimate is within ±3σ of the true cardinality.
/// Returns true if |estimate - actual| / actual < 3 * relative_error.
template <typename T, typename Hash>
static void assert_within_bounds(const probds::HyperLogLog<T, Hash>& hll,
                                 std::size_t actual_cardinality,
                                 const std::string& label) {
    const auto est = static_cast<double>(hll.estimate());
    const auto actual = static_cast<double>(actual_cardinality);
    const double rel_err = hll.relative_error();
    const double tolerance = 3.0 * rel_err;  // 3σ → 99.7% confidence

    const double observed_error = std::abs(est - actual) / actual;

    EXPECT_LT(observed_error, tolerance)
        << label
        << ": estimate=" << hll.estimate()
        << ", actual=" << actual_cardinality
        << ", observed_rel_error=" << observed_error
        << ", tolerance(3σ)=" << tolerance;
}

// =============================================================================
// Test: EmptyEstimate
// An empty HyperLogLog should estimate 0.
// =============================================================================
TEST(HyperLogLogTest, EmptyEstimate) {
    probds::HyperLogLog hll(14);
    EXPECT_EQ(hll.estimate(), 0u);
}

// =============================================================================
// Test: CardinalityAccuracy
// Insert N distinct items and verify estimate is within ±3σ of N.
// Tests at N = 1000, 10000, 100000.
// =============================================================================
TEST(HyperLogLogTest, CardinalityAccuracy1K) {
    constexpr std::size_t N = 1000;
    probds::HyperLogLog hll(14);

    for (std::size_t i = 0; i < N; ++i) {
        hll.insert(test_key(i));
    }

    assert_within_bounds(hll, N, "N=1000");
}

TEST(HyperLogLogTest, CardinalityAccuracy10K) {
    constexpr std::size_t N = 10000;
    probds::HyperLogLog hll(14);

    for (std::size_t i = 0; i < N; ++i) {
        hll.insert(test_key(i));
    }

    assert_within_bounds(hll, N, "N=10000");
}

TEST(HyperLogLogTest, CardinalityAccuracy100K) {
    constexpr std::size_t N = 100000;
    probds::HyperLogLog hll(14);

    for (std::size_t i = 0; i < N; ++i) {
        hll.insert(test_key(i));
    }

    assert_within_bounds(hll, N, "N=100000");
}

// =============================================================================
// Test: SmallCardinality
// Insert 10-100 items and verify Linear Counting provides good accuracy.
// At small cardinalities, the small-range correction should kick in.
// =============================================================================
TEST(HyperLogLogTest, SmallCardinality10) {
    constexpr std::size_t N = 10;
    probds::HyperLogLog hll(14);

    for (std::size_t i = 0; i < N; ++i) {
        hll.insert(test_key(i));
    }

    // For very small cardinalities, use absolute bounds
    const auto est = hll.estimate();
    EXPECT_GE(est, 1u) << "Estimate should be at least 1 for 10 items";
    EXPECT_LE(est, 30u) << "Estimate should be at most 30 for 10 items";
}

TEST(HyperLogLogTest, SmallCardinality100) {
    constexpr std::size_t N = 100;
    probds::HyperLogLog hll(14);

    for (std::size_t i = 0; i < N; ++i) {
        hll.insert(test_key(i));
    }

    // More generous bounds for 100 items
    const auto est = hll.estimate();
    EXPECT_GE(est, 50u) << "Estimate too low for 100 items";
    EXPECT_LE(est, 200u) << "Estimate too high for 100 items";
}

// =============================================================================
// Test: LargeCardinality
// Insert 1M items and verify estimate is within ±3σ.
// =============================================================================
TEST(HyperLogLogTest, LargeCardinality1M) {
    constexpr std::size_t N = 1000000;
    probds::HyperLogLog hll(14);

    for (std::size_t i = 0; i < N; ++i) {
        hll.insert(test_key(i));
    }

    assert_within_bounds(hll, N, "N=1000000");
}

// =============================================================================
// Test: Merge
// Create two HyperLogLogs with disjoint items, merge, and verify the
// combined estimate is close to the union cardinality.
// =============================================================================
TEST(HyperLogLogTest, Merge) {
    constexpr std::size_t N = 50000;
    probds::HyperLogLog hll_a(14);
    probds::HyperLogLog hll_b(14);

    // Insert disjoint sets: A = [0, N), B = [N, 2N)
    for (std::size_t i = 0; i < N; ++i) {
        hll_a.insert(test_key(i));
    }
    for (std::size_t i = N; i < 2 * N; ++i) {
        hll_b.insert(test_key(i));
    }

    // Merge B into A
    hll_a.merge(hll_b);

    // The union has 2N distinct elements
    assert_within_bounds(hll_a, 2 * N, "Merge(disjoint)");
}

// =============================================================================
// Test: MergeOverlapping
// Merge two sketches with overlapping items and verify the estimate is
// close to the distinct count of the union.
// =============================================================================
TEST(HyperLogLogTest, MergeOverlapping) {
    constexpr std::size_t N = 50000;
    constexpr std::size_t overlap = 25000;  // 50% overlap
    probds::HyperLogLog hll_a(14);
    probds::HyperLogLog hll_b(14);

    // A = [0, N), B = [N - overlap, 2N - overlap)
    // Union = [0, 2N - overlap), distinct count = 2N - overlap = 75000
    for (std::size_t i = 0; i < N; ++i) {
        hll_a.insert(test_key(i));
    }
    for (std::size_t i = N - overlap; i < 2 * N - overlap; ++i) {
        hll_b.insert(test_key(i));
    }

    hll_a.merge(hll_b);

    const std::size_t expected_distinct = 2 * N - overlap;
    assert_within_bounds(hll_a, expected_distinct, "Merge(overlapping)");
}

// =============================================================================
// Test: MergePrecisionMismatch
// Merging sketches with different precisions should throw.
// =============================================================================
TEST(HyperLogLogTest, MergePrecisionMismatch) {
    probds::HyperLogLog hll_a(10);
    probds::HyperLogLog hll_b(12);

    EXPECT_THROW(hll_a.merge(hll_b), std::invalid_argument);
}

// =============================================================================
// Test: Duplicates
// Inserting the same item many times should not inflate the estimate.
// =============================================================================
TEST(HyperLogLogTest, Duplicates) {
    probds::HyperLogLog hll(14);

    for (std::size_t i = 0; i < 1000; ++i) {
        hll.insert("same_key");
    }

    // Should estimate ~1 (allow up to 5 due to HLL granularity)
    EXPECT_GE(hll.estimate(), 1u);
    EXPECT_LE(hll.estimate(), 5u)
        << "Duplicate insertions should not inflate estimate";
}

// =============================================================================
// Test: Clear
// After clear(), the estimate should be 0.
// =============================================================================
TEST(HyperLogLogTest, Clear) {
    constexpr std::size_t N = 1000;
    probds::HyperLogLog hll(14);

    for (std::size_t i = 0; i < N; ++i) {
        hll.insert(test_key(i));
    }
    ASSERT_GT(hll.estimate(), 0u);

    hll.clear();

    EXPECT_EQ(hll.estimate(), 0u);
}

// =============================================================================
// Test: MemoryUsage
// Memory should be exactly 2^p bytes (one byte per register).
// =============================================================================
TEST(HyperLogLogTest, MemoryUsage) {
    for (std::uint8_t p = 4; p <= 16; ++p) {
        probds::HyperLogLog hll(p);
        const std::size_t expected_bytes = std::size_t{1} << p;
        EXPECT_EQ(hll.memory_usage(), expected_bytes)
            << "Memory mismatch for precision=" << static_cast<int>(p);
    }
}

// =============================================================================
// Test: InvalidPrecision
// Precision outside [4, 16] should throw.
// =============================================================================
TEST(HyperLogLogTest, InvalidPrecisionTooLow) {
    EXPECT_THROW(probds::HyperLogLog(3), std::invalid_argument);
    EXPECT_THROW(probds::HyperLogLog(0), std::invalid_argument);
}

TEST(HyperLogLogTest, InvalidPrecisionTooHigh) {
    EXPECT_THROW(probds::HyperLogLog(17), std::invalid_argument);
    EXPECT_THROW(probds::HyperLogLog(255), std::invalid_argument);
}

// =============================================================================
// Test: ValidPrecisionBounds
// The boundary values p=4 and p=16 should work fine.
// =============================================================================
TEST(HyperLogLogTest, ValidPrecisionBounds) {
    EXPECT_NO_THROW(probds::HyperLogLog(4));
    EXPECT_NO_THROW(probds::HyperLogLog(16));
}

// =============================================================================
// Test: RelativeError
// Verify that relative_error() returns 1.04/sqrt(2^p).
// =============================================================================
TEST(HyperLogLogTest, RelativeError) {
    for (std::uint8_t p = 4; p <= 16; ++p) {
        probds::HyperLogLog hll(p);
        const double m = static_cast<double>(std::size_t{1} << p);
        const double expected = 1.04 / std::sqrt(m);
        EXPECT_DOUBLE_EQ(hll.relative_error(), expected)
            << "Relative error mismatch for precision=" << static_cast<int>(p);
    }
}

// =============================================================================
// Test: Precision
// Verify the precision getter returns the correct value.
// =============================================================================
TEST(HyperLogLogTest, Precision) {
    for (std::uint8_t p = 4; p <= 16; ++p) {
        probds::HyperLogLog hll(p);
        EXPECT_EQ(hll.precision(), p);
    }
}

// =============================================================================
// Test: DifferentPrecisions
// Verify accuracy at different precision levels.
// Lower precision → higher error, higher precision → lower error.
// =============================================================================
TEST(HyperLogLogTest, DifferentPrecisions) {
    constexpr std::size_t N = 50000;

    for (std::uint8_t p : {4, 8, 12, 16}) {
        probds::HyperLogLog hll(p);
        for (std::size_t i = 0; i < N; ++i) {
            hll.insert(test_key(i));
        }

        // Use 5σ tolerance for low-precision HLL — the error distribution
        // has heavier tails than Gaussian at small register counts (p ≤ 8),
        // so 3σ is too tight and would cause flaky test failures.
        const auto est = static_cast<double>(hll.estimate());
        const auto actual = static_cast<double>(N);
        const double rel_err = hll.relative_error();
        const double tolerance = 5.0 * rel_err;

        const double observed_error = std::abs(est - actual) / actual;

        EXPECT_LT(observed_error, tolerance)
            << "precision=" << static_cast<int>(p)
            << ": estimate=" << hll.estimate()
            << ", actual=" << N
            << ", observed_rel_error=" << observed_error
            << ", tolerance(5σ)=" << tolerance;
    }
}

// =============================================================================
// Test: InsertAndEstimateMonotonicity
// As we insert more distinct items, the estimate should generally increase.
// (Not strictly monotonic due to randomness, but directionally correct.)
// =============================================================================
TEST(HyperLogLogTest, EstimateGrowsWithInsertions) {
    probds::HyperLogLog hll(14);

    hll.insert("key_0");
    const auto est_1 = hll.estimate();
    EXPECT_GE(est_1, 1u);

    for (std::size_t i = 1; i < 10000; ++i) {
        hll.insert(test_key(i));
    }
    const auto est_10k = hll.estimate();

    // After 10K insertions, estimate should be much larger than after 1
    EXPECT_GT(est_10k, est_1);
    EXPECT_GT(est_10k, 5000u);
}
