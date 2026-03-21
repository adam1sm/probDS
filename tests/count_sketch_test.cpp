// =============================================================================
// count_sketch_test.cpp — Tests for probds::CountSketch
// =============================================================================

#include "probds/count_sketch.hpp"
#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <vector>

TEST(CountSketchTest, BasicUsage) {
    probds::CountSketch<std::string> cs(0.01, 0.01);
    EXPECT_EQ(cs.total_count(), 0u);
    EXPECT_EQ(cs.estimate("item"), 0);

    cs.insert("item", 100);
    EXPECT_EQ(cs.total_count(), 100u);
    EXPECT_EQ(cs.estimate("item"), 100);
}

TEST(CountSketchTest, NegativeUpdates) {
    probds::CountSketch<std::string> cs(0.01, 0.01);
    cs.insert("item", 100);
    cs.insert("item", -40); // decrement

    EXPECT_EQ(cs.total_count(), 140u);
    EXPECT_EQ(cs.estimate("item"), 60);
}

TEST(CountSketchTest, UnbiasedEstimation) {
    probds::CountSketch<std::string> cs(0.01, 0.01);
    
    // Insert many items to create noise, but some positive, some negative
    for (int i = 0; i < 1000; ++i) {
        std::string key = "noise_" + std::to_string(i);
        int64_t val = (i % 2 == 0) ? 50 : -50;
        cs.insert(key, val);
    }

    // The estimate of a non-present item should be close to 0 (unbiased)
    std::int64_t est = cs.estimate("non_present");
    EXPECT_NEAR(static_cast<double>(est), 0.0, 100.0);
}

TEST(CountSketchTest, SerializationRoundTrip) {
    probds::CountSketch<std::string> cs(0.01, 0.01);
    cs.insert("apple", 50);
    cs.insert("banana", -30);

    std::stringstream ss;
    cs.serialize(ss);

    auto cs2 = probds::CountSketch<std::string>::deserialize(ss);
    EXPECT_EQ(cs2.total_count(), 80u);
    EXPECT_EQ(cs2.width(), cs.width());
    EXPECT_EQ(cs2.depth(), cs.depth());
    EXPECT_EQ(cs2.estimate("apple"), 50);
    EXPECT_EQ(cs2.estimate("banana"), -30);
    EXPECT_EQ(cs2.estimate("cherry"), 0);
}
