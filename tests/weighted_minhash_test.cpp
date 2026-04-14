// =============================================================================
// weighted_minhash_test.cpp — Tests for probds::WeightedMinHash
// =============================================================================

#include "probds/weighted_minhash.hpp"
#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <vector>

TEST(WeightedMinHashTest, BasicSimilarity) {
    probds::WeightedMinHash wmh1(1000);
    probds::WeightedMinHash wmh2(1000);

    // Identical insertion
    wmh1.insert("apple", 2.5);
    wmh1.insert("banana", 4.0);

    wmh2.insert("apple", 2.5);
    wmh2.insert("banana", 4.0);

    EXPECT_DOUBLE_EQ(wmh1.jaccard(wmh1), 1.0);
    EXPECT_DOUBLE_EQ(wmh1.jaccard(wmh2), 1.0);
}

TEST(WeightedMinHashTest, DisjointSimilarity) {
    probds::WeightedMinHash wmh1(100);
    probds::WeightedMinHash wmh2(100);

    wmh1.insert("apple", 2.5);
    wmh2.insert("banana", 4.0);

    EXPECT_DOUBLE_EQ(wmh1.jaccard(wmh2), 0.0);
}

TEST(WeightedMinHashTest, WeightedJaccardEstimation) {
    // True Jaccard between A=[3.0, 1.0] and B=[1.0, 2.0]
    // min sum: min(3, 1) + min(1, 2) = 1 + 1 = 2
    // max sum: max(3, 1) + max(1, 2) = 3 + 2 = 5
    // true similarity: 2 / 5 = 0.4
    probds::WeightedMinHash wmh1(2000);
    probds::WeightedMinHash wmh2(2000);

    wmh1.insert("item_0", 3.0);
    wmh1.insert("item_1", 1.0);

    wmh2.insert("item_0", 1.0);
    wmh2.insert("item_1", 2.0);

    double sim = wmh1.jaccard(wmh2);
    // Standard error ~ 1 / sqrt(2000) ~ 0.022
    // 3 * std_err ~ 0.066
    EXPECT_NEAR(sim, 0.4, 0.08);
}

TEST(WeightedMinHashTest, Merge) {
    probds::WeightedMinHash wmh1(500);
    probds::WeightedMinHash wmh2(500);
    probds::WeightedMinHash wmh3(500);

    wmh1.insert("apple", 2.0);
    wmh1.insert("banana", 1.0);

    wmh2.insert("banana", 3.0);
    wmh2.insert("cherry", 4.0);

    // wmh3 represents the union / maximum of weights
    wmh3.insert("apple", 2.0);
    wmh3.insert("banana", 3.0);
    wmh3.insert("cherry", 4.0);

    wmh1.merge(wmh2);

    // Merged should be identical to wmh3
    EXPECT_DOUBLE_EQ(wmh1.jaccard(wmh3), 1.0);
}

TEST(WeightedMinHashTest, SerializationRoundTrip) {
    probds::WeightedMinHash wmh(500);
    wmh.insert("apple", 2.5);
    wmh.insert("banana", 4.0);

    std::stringstream ss;
    wmh.serialize(ss);

    auto deserialized = probds::WeightedMinHash<std::string_view>::deserialize(ss);
    EXPECT_EQ(deserialized.k(), 500u);
    EXPECT_DOUBLE_EQ(deserialized.jaccard(wmh), 1.0);
}
