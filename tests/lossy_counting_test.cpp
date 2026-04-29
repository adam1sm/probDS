// =============================================================================
// lossy_counting_test.cpp — Tests for probds::LossyCounting
// =============================================================================

#include "probds/lossy_counting.hpp"
#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <vector>

TEST(LossyCountingTest, BasicInsert) {
    probds::LossyCounting<std::string> lc(0.1); // bucket width = 10

    for (int i = 0; i < 9; ++i) {
        lc.insert("A");
    }
    lc.insert("B"); // N = 10, triggers prune
    // Since B's count + error (1 + 0) = 1 <= 1, B is pruned!
    // A's count + error (9 + 0) = 9 > 1, A remains!
    EXPECT_EQ(lc.estimate("A"), 9u);
    EXPECT_EQ(lc.estimate("B"), 0u);
}

TEST(LossyCountingTest, HeavyHitters) {
    probds::LossyCounting<std::string> lc(0.01); // bucket width = 100
    for (int i = 0; i < 500; ++i) lc.insert("heavy1");
    for (int i = 0; i < 300; ++i) lc.insert("heavy2");
    for (int i = 0; i < 50; ++i) lc.insert("light");

    auto hh = lc.heavy_hitters(0.1); // threshold 10%
    EXPECT_EQ(hh.size(), 2u);

    bool found1 = false, found2 = false;
    for (const auto& pair : hh) {
        if (pair.first == "heavy1") found1 = true;
        if (pair.first == "heavy2") found2 = true;
    }
    EXPECT_TRUE(found1);
    EXPECT_TRUE(found2);
}

TEST(LossyCountingTest, Merge) {
    probds::LossyCounting<std::string> lc1(0.01);
    probds::LossyCounting<std::string> lc2(0.01);

    for (int i = 0; i < 200; ++i) {
        lc1.insert("A");
    }
    for (int i = 0; i < 100; ++i) {
        lc2.insert("A");
        lc2.insert("B");
    }

    lc1.merge(lc2);
    EXPECT_EQ(lc1.estimate("A"), 300u);
    EXPECT_EQ(lc1.estimate("B"), 100u);
}

TEST(LossyCountingTest, Serialization) {
    probds::LossyCounting<std::string_view> lc(0.05);
    lc.insert("apple");
    lc.insert("apple");
    lc.insert("banana");

    std::stringstream ss;
    lc.serialize(ss);

    auto deserialized = probds::LossyCounting<std::string_view>::deserialize(ss);
    EXPECT_DOUBLE_EQ(deserialized.epsilon(), 0.05);
    EXPECT_EQ(deserialized.total_processed(), 3u);
    EXPECT_EQ(deserialized.estimate("apple"), 2u);
    EXPECT_EQ(deserialized.estimate("banana"), 1u);
}
