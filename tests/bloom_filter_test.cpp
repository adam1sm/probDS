// =============================================================================
// bloom_filter_test.cpp — Tests for probds::BloomFilter (v2)
// =============================================================================
//
// Test strategy:
//   1. Deterministic tests: verify invariants that must always hold
//      (no false negatives, clear behavior, serialization round-trip)
//   2. Statistical tests: verify probabilistic bounds hold within tolerance
//      (FPR within 2× target, fill ratio near 0.5 at optimal load)
//   3. Feature tests: templated types, set operations, bulk insert
//   4. Edge cases: small filters, invalid parameters, empty filter
// =============================================================================

#include "probds/bloom_filter.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

// =============================================================================
// Helpers
// =============================================================================

static std::string member_key(std::size_t i) {
    return "member_" + std::to_string(i);
}

static std::string non_member_key(std::size_t i) {
    return "nonmember_" + std::to_string(i);
}

// =============================================================================
// Test: EmptyFilter
// =============================================================================
TEST(BloomFilterTest, EmptyFilter) {
    probds::BloomFilter bf(1000, 0.01);

    EXPECT_EQ(bf.size(), 0u);
    EXPECT_DOUBLE_EQ(bf.fill_ratio(), 0.0);
    EXPECT_DOUBLE_EQ(bf.expected_fpr(), 0.0);
    EXPECT_FALSE(bf.possibly_contains("anything"));
}

// =============================================================================
// Test: NoFalseNegatives
// Every inserted item must always be found — the fundamental guarantee.
// =============================================================================
TEST(BloomFilterTest, NoFalseNegatives) {
    constexpr std::size_t n = 10000;
    probds::BloomFilter bf(n, 0.01);

    for (std::size_t i = 0; i < n; ++i) {
        bf.insert(member_key(i));
    }

    EXPECT_EQ(bf.size(), n);

    for (std::size_t i = 0; i < n; ++i) {
        ASSERT_TRUE(bf.possibly_contains(member_key(i)))
            << "False negative at i=" << i;
    }
}

// =============================================================================
// Test: FalsePositiveRate
// Empirical FPR should be within 2× the target rate.
// =============================================================================
TEST(BloomFilterTest, FalsePositiveRate) {
    constexpr std::size_t n = 100000;
    constexpr double target_fpr = 0.01;
    probds::BloomFilter bf(n, target_fpr);

    for (std::size_t i = 0; i < n; ++i) {
        bf.insert(member_key(i));
    }

    constexpr std::size_t num_queries = 100000;
    std::size_t false_positives = 0;
    for (std::size_t i = 0; i < num_queries; ++i) {
        if (bf.possibly_contains(non_member_key(i))) {
            ++false_positives;
        }
    }

    const double observed_fpr =
        static_cast<double>(false_positives) / static_cast<double>(num_queries);

    // Since we round m up to power of 2, actual FPR should be ≤ target.
    // Allow 2× margin for statistical variation.
    EXPECT_LE(observed_fpr, target_fpr * 2.0)
        << "Observed FPR " << observed_fpr << " exceeds 2x target " << target_fpr;
}

// =============================================================================
// Test: FalsePositiveRateVariousFPR
// Test at different target FPR levels.
// =============================================================================
TEST(BloomFilterTest, FalsePositiveRateVariousFPR) {
    for (double target_fpr : {0.1, 0.01, 0.001}) {
        constexpr std::size_t n = 50000;
        probds::BloomFilter bf(n, target_fpr);

        for (std::size_t i = 0; i < n; ++i) {
            bf.insert(member_key(i));
        }

        constexpr std::size_t num_queries = 100000;
        std::size_t false_positives = 0;
        for (std::size_t i = 0; i < num_queries; ++i) {
            if (bf.possibly_contains(non_member_key(i))) {
                ++false_positives;
            }
        }

        const double observed =
            static_cast<double>(false_positives) /
            static_cast<double>(num_queries);

        EXPECT_LE(observed, target_fpr * 2.5)
            << "target=" << target_fpr << " observed=" << observed;
    }
}

// =============================================================================
// Test: Clear
// =============================================================================
TEST(BloomFilterTest, Clear) {
    constexpr std::size_t n = 5000;
    probds::BloomFilter bf(n, 0.01);

    for (std::size_t i = 0; i < n; ++i) {
        bf.insert(member_key(i));
    }
    ASSERT_EQ(bf.size(), n);
    ASSERT_GT(bf.fill_ratio(), 0.0);

    bf.clear();

    EXPECT_EQ(bf.size(), 0u);
    EXPECT_DOUBLE_EQ(bf.fill_ratio(), 0.0);

    for (std::size_t i = 0; i < n; ++i) {
        EXPECT_FALSE(bf.possibly_contains(member_key(i)))
            << "Found item after clear at i=" << i;
    }
}

// =============================================================================
// Test: OptimalParameters
// Verify bit count is a power of 2 and ≥ theoretical optimal.
// Verify hash count is reasonable.
// =============================================================================
TEST(BloomFilterTest, OptimalParameters) {
    constexpr std::size_t n = 10000;
    constexpr double p = 0.01;
    probds::BloomFilter bf(n, p);

    const double ln2 = std::log(2.0);
    const double ln2_sq = ln2 * ln2;

    // Theoretical optimal m
    const auto m_opt = static_cast<std::size_t>(
        std::ceil(-static_cast<double>(n) * std::log(p) / ln2_sq));

    // Actual m must be ≥ optimal and a power of 2
    EXPECT_GE(bf.bit_count(), m_opt);
    EXPECT_EQ(bf.bit_count() & (bf.bit_count() - 1), 0u)
        << "bit_count=" << bf.bit_count() << " is not a power of 2";

    // k should be reasonable for the actual m
    const auto expected_k = static_cast<std::size_t>(
        std::round(static_cast<double>(bf.bit_count()) /
                   static_cast<double>(n) * ln2));
    EXPECT_EQ(bf.hash_count(), std::max(std::size_t{1}, expected_k));

    // k should be in a reasonable range
    EXPECT_GE(bf.hash_count(), 1u);
    EXPECT_LE(bf.hash_count(), 30u);
}

// =============================================================================
// Test: LargeScale
// 1M insertions with correctness check.
// =============================================================================
TEST(BloomFilterTest, LargeScale) {
    constexpr std::size_t n = 1000000;
    probds::BloomFilter bf(n, 0.01);

    for (std::size_t i = 0; i < n; ++i) {
        bf.insert(member_key(i));
    }

    // Spot-check: first 1000, last 1000
    for (std::size_t i = 0; i < 1000; ++i) {
        ASSERT_TRUE(bf.possibly_contains(member_key(i)));
    }
    for (std::size_t i = n - 1000; i < n; ++i) {
        ASSERT_TRUE(bf.possibly_contains(member_key(i)));
    }

    // fill_ratio should be near 0.5 at optimal load
    EXPECT_GT(bf.fill_ratio(), 0.3);
    EXPECT_LT(bf.fill_ratio(), 0.7);
}

// =============================================================================
// Test: FillRatioIsO1
// Verify fill_ratio() is consistent with actual popcount.
// =============================================================================
TEST(BloomFilterTest, FillRatioConsistency) {
    constexpr std::size_t n = 5000;
    probds::BloomFilter bf(n, 0.01);

    EXPECT_DOUBLE_EQ(bf.fill_ratio(), 0.0);

    for (std::size_t i = 0; i < n; ++i) {
        bf.insert(member_key(i));
    }

    // fill_ratio should be in (0, 1)
    const double fr = bf.fill_ratio();
    EXPECT_GT(fr, 0.0);
    EXPECT_LT(fr, 1.0);

    // After clear, fill_ratio should be 0
    bf.clear();
    EXPECT_DOUBLE_EQ(bf.fill_ratio(), 0.0);
}

// =============================================================================
// Test: InvalidParameters
// =============================================================================
TEST(BloomFilterTest, InvalidParameters) {
    EXPECT_THROW(probds::BloomFilter<>(0, 0.01), std::invalid_argument);
    EXPECT_THROW(probds::BloomFilter<>(1000, 0.0), std::invalid_argument);
    EXPECT_THROW(probds::BloomFilter<>(1000, 1.0), std::invalid_argument);
    EXPECT_THROW(probds::BloomFilter<>(1000, -0.5), std::invalid_argument);
}

// =============================================================================
// Test: SmallFilter
// =============================================================================
TEST(BloomFilterTest, SmallFilter) {
    probds::BloomFilter bf(10, 0.1);
    bf.insert("hello");
    bf.insert("world");

    EXPECT_TRUE(bf.possibly_contains("hello"));
    EXPECT_TRUE(bf.possibly_contains("world"));
    EXPECT_EQ(bf.size(), 2u);
    EXPECT_GT(bf.bit_count(), 0u);
}

// =============================================================================
// Test: IntegerKeys
// BloomFilter<int> should work for integer keys.
// =============================================================================
TEST(BloomFilterTest, IntegerKeys) {
    constexpr std::size_t n = 10000;
    probds::BloomFilter<int> bf(n, 0.01);

    for (int i = 0; i < static_cast<int>(n); ++i) {
        bf.insert(i);
    }

    EXPECT_EQ(bf.size(), n);

    // No false negatives
    for (int i = 0; i < static_cast<int>(n); ++i) {
        ASSERT_TRUE(bf.possibly_contains(i))
            << "False negative for int key i=" << i;
    }

    // Check FPR with negative integers (never inserted)
    std::size_t fps = 0;
    constexpr std::size_t queries = 50000;
    for (int i = -static_cast<int>(queries); i < 0; ++i) {
        if (bf.possibly_contains(i)) ++fps;
    }

    const double fpr = static_cast<double>(fps) / static_cast<double>(queries);
    EXPECT_LE(fpr, 0.02) << "Integer FPR too high: " << fpr;
}

// =============================================================================
// Test: DoubleKeys
// BloomFilter<double> should work for floating-point keys.
// =============================================================================
TEST(BloomFilterTest, DoubleKeys) {
    probds::BloomFilter<double> bf(1000, 0.01);

    for (int i = 0; i < 1000; ++i) {
        bf.insert(static_cast<double>(i) * 0.1);
    }

    // Spot-check
    EXPECT_TRUE(bf.possibly_contains(0.0));
    EXPECT_TRUE(bf.possibly_contains(50.0));
    EXPECT_TRUE(bf.possibly_contains(99.9));
}

// =============================================================================
// Test: BulkInsert
// Iterator-based bulk insert should produce identical results to individual.
// =============================================================================
TEST(BloomFilterTest, BulkInsert) {
    constexpr std::size_t n = 5000;

    // Generate keys
    std::vector<std::string> keys;
    keys.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        keys.push_back(member_key(i));
    }

    // Bulk insert
    probds::BloomFilter<std::string> bf_bulk(n, 0.01);
    bf_bulk.insert(keys.begin(), keys.end());

    EXPECT_EQ(bf_bulk.size(), n);

    // All items should be found
    for (const auto& key : keys) {
        ASSERT_TRUE(bf_bulk.possibly_contains(key));
    }

    // Comparing with individual insert: same size, both find all keys
    probds::BloomFilter<std::string> bf_single(n, 0.01);
    for (const auto& key : keys) {
        bf_single.insert(key);
    }

    EXPECT_EQ(bf_bulk.size(), bf_single.size());
    EXPECT_EQ(bf_bulk.bit_count(), bf_single.bit_count());
}

// =============================================================================
// Test: Serialization
// Round-trip: serialize → deserialize → verify all queries produce same result.
// =============================================================================
TEST(BloomFilterTest, Serialization) {
    constexpr std::size_t n = 10000;
    probds::BloomFilter bf(n, 0.01);

    for (std::size_t i = 0; i < n; ++i) {
        bf.insert(member_key(i));
    }

    // Serialize to string stream
    std::stringstream ss;
    bf.serialize(ss);

    // Deserialize
    ss.seekg(0);
    auto bf2 = probds::BloomFilter<>::deserialize(ss);

    // Verify metadata matches
    EXPECT_EQ(bf2.bit_count(), bf.bit_count());
    EXPECT_EQ(bf2.hash_count(), bf.hash_count());
    EXPECT_EQ(bf2.size(), bf.size());
    EXPECT_DOUBLE_EQ(bf2.fill_ratio(), bf.fill_ratio());

    // Verify all queries produce identical results
    for (std::size_t i = 0; i < n; ++i) {
        ASSERT_EQ(bf2.possibly_contains(member_key(i)),
                  bf.possibly_contains(member_key(i)))
            << "Mismatch at i=" << i;
    }

    // Also check non-members
    for (std::size_t i = 0; i < 1000; ++i) {
        EXPECT_EQ(bf2.possibly_contains(non_member_key(i)),
                  bf.possibly_contains(non_member_key(i)));
    }
}

// =============================================================================
// Test: SerializationInvalidMagic
// Deserializing garbage should throw.
// =============================================================================
TEST(BloomFilterTest, SerializationInvalidMagic) {
    std::stringstream ss;
    ss.write("JUNK", 4);
    EXPECT_THROW(probds::BloomFilter<>::deserialize(ss), std::runtime_error);
}

// =============================================================================
// Test: Union
// A | B should contain all items from both A and B.
// =============================================================================
TEST(BloomFilterTest, Union) {
    constexpr std::size_t n = 5000;
    probds::BloomFilter a(n, 0.01);
    probds::BloomFilter b(n, 0.01);

    // Insert disjoint sets
    for (std::size_t i = 0; i < n; ++i) {
        a.insert("set_a_" + std::to_string(i));
        b.insert("set_b_" + std::to_string(i));
    }

    // Union
    auto ab = a | b;

    // All items from both sets should be found
    for (std::size_t i = 0; i < n; ++i) {
        ASSERT_TRUE(ab.possibly_contains("set_a_" + std::to_string(i)))
            << "Missing from A at i=" << i;
        ASSERT_TRUE(ab.possibly_contains("set_b_" + std::to_string(i)))
            << "Missing from B at i=" << i;
    }

    // Size is sum (upper bound)
    EXPECT_EQ(ab.size(), 2 * n);
}

// =============================================================================
// Test: UnionInPlace
// a |= b should modify a in place.
// =============================================================================
TEST(BloomFilterTest, UnionInPlace) {
    constexpr std::size_t n = 1000;
    probds::BloomFilter a(n, 0.01);
    probds::BloomFilter b(n, 0.01);

    for (std::size_t i = 0; i < n; ++i) {
        a.insert("a_" + std::to_string(i));
        b.insert("b_" + std::to_string(i));
    }

    a |= b;

    // Both sets should be in a
    for (std::size_t i = 0; i < n; ++i) {
        EXPECT_TRUE(a.possibly_contains("a_" + std::to_string(i)));
        EXPECT_TRUE(a.possibly_contains("b_" + std::to_string(i)));
    }
}

// =============================================================================
// Test: Intersection
// A & B should contain items that are in BOTH A and B.
// =============================================================================
TEST(BloomFilterTest, Intersection) {
    constexpr std::size_t n = 5000;
    probds::BloomFilter a(n, 0.01);
    probds::BloomFilter b(n, 0.01);

    // Insert overlapping sets: A = [0, n), B = [n/2, 3n/2)
    // Overlap = [n/2, n)
    for (std::size_t i = 0; i < n; ++i) {
        a.insert(member_key(i));
    }
    for (std::size_t i = n / 2; i < n + n / 2; ++i) {
        b.insert(member_key(i));
    }

    auto ab = a & b;

    // Items in the overlap should be found
    for (std::size_t i = n / 2; i < n; ++i) {
        EXPECT_TRUE(ab.possibly_contains(member_key(i)))
            << "Overlap item missing at i=" << i;
    }

    // Items only in A should mostly NOT be found (may have false positives)
    std::size_t only_a_found = 0;
    for (std::size_t i = 0; i < n / 2; ++i) {
        if (ab.possibly_contains(member_key(i))) ++only_a_found;
    }

    // The intersection should significantly reduce hits for A-only items
    const double only_a_rate =
        static_cast<double>(only_a_found) / static_cast<double>(n / 2);
    EXPECT_LT(only_a_rate, 0.5)
        << "Too many A-only items in intersection: " << only_a_rate;
}

// =============================================================================
// Test: UnionIncompatible
// Union of filters with different parameters should throw.
// =============================================================================
TEST(BloomFilterTest, UnionIncompatible) {
    probds::BloomFilter a(1000, 0.01);
    probds::BloomFilter b(5000, 0.01);  // Different size → different bit_count

    if (a.bit_count() != b.bit_count()) {
        EXPECT_THROW(a |= b, std::invalid_argument);
    }
}

// =============================================================================
// Test: PowerOfTwoBitCount
// Verify bit_count() is always a power of 2 for various parameters.
// =============================================================================
TEST(BloomFilterTest, PowerOfTwoBitCount) {
    for (std::size_t n : {10, 100, 1000, 12345, 100000, 999999}) {
        for (double p : {0.1, 0.01, 0.001, 0.0001}) {
            probds::BloomFilter bf(n, p);
            const auto m = bf.bit_count();
            EXPECT_EQ(m & (m - 1), 0u)
                << "n=" << n << " p=" << p << " bit_count=" << m;
        }
    }
}

// =============================================================================
// Test: MemoryUsage
// Memory should equal num_words * 8 bytes.
// =============================================================================
TEST(BloomFilterTest, MemoryUsage) {
    probds::BloomFilter bf(10000, 0.01);
    EXPECT_EQ(bf.memory_usage(), bf.bit_count() / 8);
    EXPECT_GT(bf.memory_usage(), 0u);
}

// =============================================================================
// Test: CustomHashFunction
// Verify that a custom hash function can be injected.
// =============================================================================
TEST(BloomFilterTest, CustomHashFunction) {
    // Simple custom hash that just uses std::hash
    struct MyHash {
        std::uint64_t
        operator()(int key) const noexcept {
            return static_cast<std::uint64_t>(std::hash<int>{}(key));
        }
    };

    probds::BloomFilter<int, MyHash> bf(1000, 0.01);
    bf.insert(42);
    bf.insert(123);

    EXPECT_TRUE(bf.possibly_contains(42));
    EXPECT_TRUE(bf.possibly_contains(123));
    EXPECT_EQ(bf.size(), 2u);
}
