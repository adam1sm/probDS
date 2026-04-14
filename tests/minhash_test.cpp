// =============================================================================
// minhash_test.cpp — Tests for probds::MinHash
// =============================================================================

#include "probds/minhash.hpp"
#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <vector>

TEST(MinHashTest, EmptySignature) {
    probds::MinHash mh(100);
    EXPECT_EQ(mh.k(), 100u);
    EXPECT_EQ(mh.size(), 0u);
    for (auto val : mh.signature()) {
        EXPECT_EQ(val, std::numeric_limits<std::uint64_t>::max());
    }
}

TEST(MinHashTest, JaccardEstimation) {
    // k = 500 to get low variance
    probds::MinHash mh1(500);
    probds::MinHash mh2(500);
    probds::MinHash mh3(500);

    // mh1: 0..99
    // mh2: 50..149 (50% overlap with mh1)
    // mh3: 200..299 (disjoint from mh1 and mh2)

    for (int i = 0; i < 100; ++i) {
        std::string key = "item_" + std::to_string(i);
        mh1.insert(key);
    }

    for (int i = 50; i < 150; ++i) {
        std::string key = "item_" + std::to_string(i);
        mh2.insert(key);
    }

    for (int i = 200; i < 300; ++i) {
        std::string key = "item_" + std::to_string(i);
        mh3.insert(key);
    }

    // Identical similarity
    EXPECT_DOUBLE_EQ(mh1.jaccard_similarity(mh1), 1.0);

    // Disjoint similarity
    EXPECT_DOUBLE_EQ(mh1.jaccard_similarity(mh3), 0.0);

    // Overlapping similarity: true similarity is 50 / 150 = 1/3 ~ 0.333
    double sim = mh1.jaccard_similarity(mh2);
    // Bounded by standard error 1/sqrt(k) ~ 1/sqrt(500) ~ 0.045
    EXPECT_NEAR(sim, 0.333, 3 * 0.045);
}

TEST(MinHashTest, Merge) {
    probds::MinHash mh1(200);
    probds::MinHash mh2(200);
    probds::MinHash mh_merged(200);

    for (int i = 0; i < 100; ++i) {
        mh1.insert("item_" + std::to_string(i));
        mh_merged.insert("item_" + std::to_string(i));
    }
    for (int i = 50; i < 150; ++i) {
        mh2.insert("item_" + std::to_string(i));
    }

    mh1.merge(mh2);

    for (std::size_t i = 0; i < 200; ++i) {
        // Find expected min hash manually
        std::uint64_t m1 = mh_merged.signature()[i];
        for (int j = 100; j < 150; ++j) {
            // Recompute manually or check if merged signature is element-wise min
            m1 = std::min(m1, mh2.signature()[i]);
        }
        EXPECT_EQ(mh1.signature()[i], std::min(mh_merged.signature()[i], mh2.signature()[i]));
    }
}

TEST(MinHashTest, Serialization) {
    probds::MinHash mh(100);
    for (int i = 0; i < 50; ++i) {
        mh.insert("data_" + std::to_string(i));
    }

    std::stringstream ss;
    mh.serialize(ss);

    auto deserialized = probds::MinHash<std::string_view>::deserialize(ss);
    EXPECT_EQ(deserialized.k(), 100u);
    EXPECT_EQ(deserialized.size(), 50u);
    EXPECT_EQ(deserialized.signature(), mh.signature());
}
