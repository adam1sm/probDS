// =============================================================================
// bbit_minhash_test.cpp — Tests for probds::BBitMinHash
// =============================================================================

#include "probds/bbit_minhash.hpp"
#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <vector>

TEST(BBitMinHashTest, EmptySignature) {
    probds::BBitMinHash mh(100, 2);
    EXPECT_EQ(mh.k(), 100u);
    EXPECT_EQ(mh.b(), 2u);
    EXPECT_EQ(mh.size(), 0u);
    EXPECT_EQ(mh.memory_bytes(), sizeof(probds::BBitMinHash<std::string_view>) + 25); // (100 * 2 + 7) / 8 = 25
}

TEST(BBitMinHashTest, JaccardEstimation) {
    // Large k to get low variance for b-bit minhash
    probds::BBitMinHash mh1(2000, 2);
    probds::BBitMinHash mh2(2000, 2);
    probds::BBitMinHash mh3(2000, 2);

    // mh1: 0..99
    // mh2: 50..149 (50% overlap with mh1)
    // mh3: 200..299 (disjoint from mh1)

    for (int i = 0; i < 100; ++i) {
        mh1.insert("item_" + std::to_string(i));
    }
    for (int i = 50; i < 150; ++i) {
        mh2.insert("item_" + std::to_string(i));
    }
    for (int i = 200; i < 300; ++i) {
        mh3.insert("item_" + std::to_string(i));
    }

    EXPECT_DOUBLE_EQ(mh1.jaccard_similarity(mh1), 1.0);

    // Disjoint sets should estimate close to 0.0
    double disjoint_sim = mh1.jaccard_similarity(mh3);
    EXPECT_NEAR(disjoint_sim, 0.0, 0.05);

    // Overlapping sets: true similarity is 50 / 150 = 1/3 ~ 0.333
    double sim = mh1.jaccard_similarity(mh2);
    EXPECT_NEAR(sim, 0.333, 0.08);
}

TEST(BBitMinHashTest, Merge) {
    probds::BBitMinHash mh1(200, 4);
    probds::BBitMinHash mh2(200, 4);
    probds::BBitMinHash mh_merged(200, 4);

    for (int i = 0; i < 100; ++i) {
        mh1.insert("item_" + std::to_string(i));
        mh_merged.insert("item_" + std::to_string(i));
    }
    for (int i = 50; i < 150; ++i) {
        mh2.insert("item_" + std::to_string(i));
    }

    mh1.merge(mh2);

    // Check that merged signature matches element-wise min of mh_merged and mh2
    for (std::size_t i = 0; i < 200; ++i) {
        EXPECT_EQ(mh1.signature()[i], std::min(mh_merged.signature()[i], mh2.signature()[i]));
    }
}

TEST(BBitMinHashTest, SerializationRoundTrip) {
    probds::BBitMinHash mh(128, 3);
    for (int i = 0; i < 50; ++i) {
        mh.insert("data_" + std::to_string(i));
    }

    std::stringstream ss;
    mh.serialize(ss);

    auto deserialized = probds::BBitMinHash<std::string_view>::deserialize(ss);
    EXPECT_EQ(deserialized.k(), 128u);
    EXPECT_EQ(deserialized.b(), 3u);
    EXPECT_EQ(deserialized.size(), 50u);
    EXPECT_DOUBLE_EQ(deserialized.jaccard_similarity(mh), 1.0);
}
