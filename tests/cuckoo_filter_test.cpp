// =============================================================================
// cuckoo_filter_test.cpp — Tests for probds::CuckooFilter
// =============================================================================
//
// Test strategy:
//   1. Deterministic tests: verify invariants that must always hold
//      (no false negatives, deletion correctness, clear behavior)
//   2. Statistical tests: verify probabilistic bounds hold within tolerance
//      (FPR within 3× theoretical, deletion reduces FPR, etc.)
//   3. Edge cases: small filters, invalid parameters, full-filter behavior
//
// Statistical tests use generous margins (2-3×) to avoid flakiness while
// still catching gross implementation errors.
// =============================================================================

#include "probds/cuckoo_filter.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <unordered_set>
#include <vector>

// =============================================================================
// Helpers
// =============================================================================

static std::string member_key(std::size_t i) {
    return "item_" + std::to_string(i);
}

static std::string non_member_key(std::size_t i) {
    return "nonmember_" + std::to_string(i);
}

// =============================================================================
// Test: NoFalseNegatives
// Insert N items, verify every single one is found. This is the fundamental
// guarantee — zero false negatives (assuming no failed insertions).
// =============================================================================
TEST(CuckooFilterTest, NoFalseNegatives) {
    constexpr std::size_t n = 10000;
    // Over-provision to avoid insertion failures during this test
    probds::CuckooFilter cf(n * 2);

    for (std::size_t i = 0; i < n; ++i) {
        ASSERT_TRUE(cf.insert(member_key(i)))
            << "Insertion failed at i=" << i;
    }

    EXPECT_EQ(cf.size(), n);

    // Every inserted item must be found — no exceptions
    for (std::size_t i = 0; i < n; ++i) {
        ASSERT_TRUE(cf.possibly_contains(member_key(i)))
            << "False negative at i=" << i;
    }
}

// =============================================================================
// Test: FalsePositiveRate
// Empirical FPR should be within 3× the theoretical bound.
//
// Theoretical FPR = 2 * bucket_size / 2^fingerprint_bits
//                 = 2 * 4 / 256 = 0.03125
//
// For 50000 queries on non-members:
//   Expected FP ≈ 50000 * 0.03125 = 1562.5
//   3× bound = 50000 * 0.09375 = 4687.5
//
// This only fails on genuine bugs, not statistical flukes.
// =============================================================================
TEST(CuckooFilterTest, FalsePositiveRate) {
    constexpr std::size_t n = 10000;
    probds::CuckooFilter cf(n * 2);  // Over-provision

    for (std::size_t i = 0; i < n; ++i) {
        ASSERT_TRUE(cf.insert(member_key(i)));
    }

    constexpr std::size_t num_queries = 50000;
    std::size_t false_positives = 0;
    for (std::size_t i = 0; i < num_queries; ++i) {
        if (cf.possibly_contains(non_member_key(i))) {
            ++false_positives;
        }
    }

    const double observed_fpr =
        static_cast<double>(false_positives) / static_cast<double>(num_queries);
    const double theoretical_fpr = cf.expected_fpr();  // 2*4/256 ≈ 0.03125

    EXPECT_LE(observed_fpr, theoretical_fpr * 3.0)
        << "Observed FPR " << observed_fpr
        << " exceeds 3x theoretical " << theoretical_fpr;

    // Sanity: FPR should not be zero (filter has items, collisions expected)
    EXPECT_GT(false_positives, 0u)
        << "Suspiciously zero false positives — check hashing";
}

// =============================================================================
// Test: Deletion
// Insert items, delete some, verify:
//   1. Deleted items are no longer found (most of the time — fingerprint
//      collisions may cause a false positive, but the rate should be ≤ FPR)
//   2. Non-deleted items are still all found
// =============================================================================
TEST(CuckooFilterTest, Deletion) {
    constexpr std::size_t n = 5000;
    probds::CuckooFilter cf(n * 2);

    // Insert all items
    for (std::size_t i = 0; i < n; ++i) {
        ASSERT_TRUE(cf.insert(member_key(i)));
    }
    EXPECT_EQ(cf.size(), n);

    // Delete the first half
    const std::size_t to_delete = n / 2;
    for (std::size_t i = 0; i < to_delete; ++i) {
        EXPECT_TRUE(cf.remove(member_key(i)))
            << "Remove failed at i=" << i;
    }
    EXPECT_EQ(cf.size(), n - to_delete);

    // Non-deleted items (second half) must all still be found
    for (std::size_t i = to_delete; i < n; ++i) {
        ASSERT_TRUE(cf.possibly_contains(member_key(i)))
            << "False negative for non-deleted item at i=" << i;
    }

    // Deleted items should mostly NOT be found.
    // Some may still appear as false positives (fingerprint collision with
    // remaining items), but this should be rare (≤ FPR).
    std::size_t still_found = 0;
    for (std::size_t i = 0; i < to_delete; ++i) {
        if (cf.possibly_contains(member_key(i))) {
            ++still_found;
        }
    }

    // At most ~FPR fraction of deleted items should still show as positive
    const double still_found_rate =
        static_cast<double>(still_found) / static_cast<double>(to_delete);
    EXPECT_LE(still_found_rate, cf.expected_fpr() * 3.0)
        << "Too many deleted items still found: " << still_found
        << " / " << to_delete;
}

// =============================================================================
// Test: DeletionReducesFPR
// After removing items, the effective FPR should decrease because there
// are fewer fingerprints to collide with.
// =============================================================================
TEST(CuckooFilterTest, DeletionReducesFPR) {
    constexpr std::size_t n = 5000;
    probds::CuckooFilter cf(n * 2);

    for (std::size_t i = 0; i < n; ++i) {
        ASSERT_TRUE(cf.insert(member_key(i)));
    }

    // Measure FPR before deletion
    constexpr std::size_t num_queries = 50000;
    std::size_t fp_before = 0;
    for (std::size_t i = 0; i < num_queries; ++i) {
        if (cf.possibly_contains(non_member_key(i))) {
            ++fp_before;
        }
    }

    // Delete half the items
    for (std::size_t i = 0; i < n / 2; ++i) {
        cf.remove(member_key(i));
    }

    // Measure FPR after deletion
    std::size_t fp_after = 0;
    for (std::size_t i = 0; i < num_queries; ++i) {
        if (cf.possibly_contains(non_member_key(i))) {
            ++fp_after;
        }
    }

    // FPR should decrease after removing items (fewer fingerprints = fewer collisions)
    EXPECT_LE(fp_after, fp_before)
        << "FPR did not decrease after deletion: "
        << "before=" << fp_before << " after=" << fp_after;
}

// =============================================================================
// Test: InsertionFailure
// When the filter is full, insert() should return false.
// We insert significantly more items than the capacity to trigger failures.
// =============================================================================
TEST(CuckooFilterTest, InsertionFailure) {
    // Small capacity to fill quickly
    constexpr std::size_t cap = 100;
    probds::CuckooFilter cf(cap, 4, 8);

    std::size_t inserted = 0;
    bool saw_failure = false;

    // Try to insert way more than capacity
    for (std::size_t i = 0; i < cap * 5; ++i) {
        if (cf.insert(member_key(i))) {
            ++inserted;
        } else {
            saw_failure = true;
            break;
        }
    }

    EXPECT_TRUE(saw_failure)
        << "Expected insertion failure but inserted " << inserted << " items";

    // Verify no false negatives for successfully inserted items.
    // NOTE: When the kick chain fails, one previously-inserted item's
    // fingerprint may have been evicted and lost. We allow for at most
    // 1 false negative from the failed kick chain's collateral damage.
    std::size_t false_negatives = 0;
    for (std::size_t i = 0; i < inserted; ++i) {
        if (!cf.possibly_contains(member_key(i))) {
            ++false_negatives;
        }
    }
    EXPECT_LE(false_negatives, 1u)
        << "Too many false negatives (" << false_negatives
        << ") for " << inserted << " successfully inserted items";
}

// =============================================================================
// Test: Clear
// After clear(), size should be 0 and all queries should return false.
// =============================================================================
TEST(CuckooFilterTest, Clear) {
    constexpr std::size_t n = 1000;
    probds::CuckooFilter cf(n * 2);

    for (std::size_t i = 0; i < n; ++i) {
        cf.insert(member_key(i));
    }
    ASSERT_EQ(cf.size(), n);
    ASSERT_GT(cf.load_factor(), 0.0);

    cf.clear();

    EXPECT_EQ(cf.size(), 0u);
    EXPECT_DOUBLE_EQ(cf.load_factor(), 0.0);

    // All queries should return false after clear
    for (std::size_t i = 0; i < n; ++i) {
        EXPECT_FALSE(cf.possibly_contains(member_key(i)))
            << "Found item after clear at i=" << i;
    }
}

// =============================================================================
// Test: LoadFactor
// Verify load_factor() returns sensible values at various fill levels.
// =============================================================================
TEST(CuckooFilterTest, LoadFactor) {
    constexpr std::size_t n = 5000;
    probds::CuckooFilter cf(n * 2);

    EXPECT_DOUBLE_EQ(cf.load_factor(), 0.0);

    // Insert some items and check load factor increases
    for (std::size_t i = 0; i < n; ++i) {
        cf.insert(member_key(i));
    }

    const double lf = cf.load_factor();
    EXPECT_GT(lf, 0.0);
    EXPECT_LE(lf, 1.0);

    // load_factor = size / capacity
    const double expected_lf =
        static_cast<double>(cf.size()) / static_cast<double>(cf.capacity());
    EXPECT_DOUBLE_EQ(lf, expected_lf);
}

// =============================================================================
// Test: SmallFilter
// Edge case: very small capacity. Should still function correctly.
// =============================================================================
TEST(CuckooFilterTest, SmallFilter) {
    probds::CuckooFilter cf(1);

    // Should be able to insert at least 1 item (bucket_size = 4, rounded up)
    EXPECT_TRUE(cf.insert("hello"));
    EXPECT_TRUE(cf.possibly_contains("hello"));
    EXPECT_EQ(cf.size(), 1u);

    // Remove it
    EXPECT_TRUE(cf.remove("hello"));
    EXPECT_EQ(cf.size(), 0u);
}

// =============================================================================
// Test: InvalidParameters
// Verify that invalid construction parameters throw.
// =============================================================================
TEST(CuckooFilterTest, InvalidParameters) {
    EXPECT_THROW(probds::CuckooFilter(0), std::invalid_argument);
    EXPECT_THROW(probds::CuckooFilter(100, 0), std::invalid_argument);
    EXPECT_THROW(probds::CuckooFilter(100, 4, 0), std::invalid_argument);
    EXPECT_THROW(probds::CuckooFilter(100, 4, 9), std::invalid_argument);
}

// =============================================================================
// Test: RemoveNonExistent
// Removing an item that was never inserted should return false.
// =============================================================================
TEST(CuckooFilterTest, RemoveNonExistent) {
    probds::CuckooFilter cf(1000);

    // Insert some items
    for (std::size_t i = 0; i < 100; ++i) {
        cf.insert(member_key(i));
    }

    // Removing a non-member should return false (unless fingerprint collision)
    // We test with many non-members; most should return false
    std::size_t remove_succeeded = 0;
    for (std::size_t i = 0; i < 100; ++i) {
        if (cf.remove(non_member_key(i))) {
            ++remove_succeeded;
        }
    }

    // At most FPR fraction should succeed (fingerprint collision in candidate buckets)
    const double success_rate =
        static_cast<double>(remove_succeeded) / 100.0;
    EXPECT_LE(success_rate, cf.expected_fpr() * 3.0)
        << "Too many non-existent items 'removed': " << remove_succeeded;
}

// =============================================================================
// Test: MemoryUsage
// Verify memory_usage() returns a sensible value.
// =============================================================================
TEST(CuckooFilterTest, MemoryUsage) {
    constexpr std::size_t cap = 10000;
    constexpr std::size_t b = 4;
    probds::CuckooFilter cf(cap, b, 8);

    // memory_usage = num_buckets * bucket_size * 1 byte
    // capacity = num_buckets * bucket_size
    // So memory_usage == capacity (in bytes, for 8-bit fingerprints)
    EXPECT_EQ(cf.memory_usage(), cf.capacity());
    EXPECT_GT(cf.memory_usage(), 0u);
}

// =============================================================================
// Test: ReinsertAfterDelete
// Insert, delete, then reinsert the same items. All should be found.
// =============================================================================
TEST(CuckooFilterTest, ReinsertAfterDelete) {
    constexpr std::size_t n = 1000;
    probds::CuckooFilter cf(n * 2);

    // Insert
    for (std::size_t i = 0; i < n; ++i) {
        ASSERT_TRUE(cf.insert(member_key(i)));
    }

    // Delete all
    for (std::size_t i = 0; i < n; ++i) {
        cf.remove(member_key(i));
    }
    EXPECT_EQ(cf.size(), 0u);

    // Reinsert all
    for (std::size_t i = 0; i < n; ++i) {
        ASSERT_TRUE(cf.insert(member_key(i)))
            << "Reinsertion failed at i=" << i;
    }
    EXPECT_EQ(cf.size(), n);

    // All should be found
    for (std::size_t i = 0; i < n; ++i) {
        ASSERT_TRUE(cf.possibly_contains(member_key(i)))
            << "False negative after reinsertion at i=" << i;
    }
}
