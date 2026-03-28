// =============================================================================
// ams_sketch_test.cpp — Tests for probds::AMSSketch
// =============================================================================

#include "probds/ams_sketch.hpp"
#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <vector>

TEST(AMSSketchTest, EmptyAndBasic) {
    probds::AMSSketch<std::string> ams(5, 64);
    EXPECT_DOUBLE_EQ(ams.estimate_f2(), 0.0);

    ams.insert("item1", 10);
    // F2 should be exactly 10^2 = 100 for a single element
    EXPECT_DOUBLE_EQ(ams.estimate_f2(), 100.0);
}

TEST(AMSSketchTest, MultipleElements) {
    probds::AMSSketch<std::string> ams(7, 128);

    // F2 = 5^2 + 3^2 + 2^2 = 25 + 9 + 4 = 38
    ams.insert("item1", 5);
    ams.insert("item2", 3);
    ams.insert("item3", 2);

    double est = ams.estimate_f2();
    // Bounded relative error due to depth and width
    EXPECT_NEAR(est, 38.0, 10.0);
}

TEST(AMSSketchTest, Merge) {
    probds::AMSSketch<std::string> ams1(5, 64);
    probds::AMSSketch<std::string> ams2(5, 64);

    ams1.insert("item1", 5);
    ams2.insert("item2", 10);

    ams1.merge(ams2);
    // F2 = 5^2 + 10^2 = 125
    EXPECT_DOUBLE_EQ(ams1.estimate_f2(), 125.0);
}

TEST(AMSSketchTest, Serialization) {
    probds::AMSSketch<std::string_view> ams(5, 32);
    ams.insert("apple", 3);
    ams.insert("banana", 4);

    std::stringstream ss;
    ams.serialize(ss);

    auto deserialized = probds::AMSSketch<std::string_view>::deserialize(ss);
    EXPECT_EQ(deserialized.depth(), 5u);
    EXPECT_EQ(deserialized.width(), 32u);
    EXPECT_DOUBLE_EQ(deserialized.estimate_f2(), ams.estimate_f2());
}
