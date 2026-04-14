// =============================================================================
// odd_sketch_test.cpp — Tests for probds::OddSketch
// =============================================================================

#include "probds/odd_sketch.hpp"
#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <vector>

TEST(OddSketchTest, BasicOperations) {
    probds::OddSketch sketch(1024);
    EXPECT_EQ(sketch.m(), 1024u);
    EXPECT_EQ(sketch.cardinality(), 0.0);

    sketch.insert("apple");
    EXPECT_GT(sketch.cardinality(), 0.0);
    EXPECT_NEAR(sketch.cardinality(), 1.0, 0.1);
}

TEST(OddSketchTest, SymmetricDifferenceDisjoint) {
    // Large size m to reduce variance
    probds::OddSketch sketch1(4096);
    probds::OddSketch sketch2(4096);

    // Insert 50 disjoint elements in each
    for (int i = 0; i < 50; ++i) {
        sketch1.insert("set1_" + std::to_string(i));
        sketch2.insert("set2_" + std::to_string(i));
    }

    double diff = sketch1.symmetric_difference(sketch2);
    // Expected symmetric difference is 100
    EXPECT_NEAR(diff, 100.0, 10.0);
}

TEST(OddSketchTest, SymmetricDifferenceOverlapping) {
    probds::OddSketch sketch1(8192);
    probds::OddSketch sketch2(8192);

    // sketch1: items 0..99 (size 100)
    // sketch2: items 50..149 (size 100)
    // overlap: 50
    // symmetric difference: 100
    for (int i = 0; i < 100; ++i) {
        sketch1.insert("item_" + std::to_string(i));
    }
    for (int i = 50; i < 150; ++i) {
        sketch2.insert("item_" + std::to_string(i));
    }

    double diff = sketch1.symmetric_difference(sketch2);
    EXPECT_NEAR(diff, 100.0, 10.0);
}

TEST(OddSketchTest, Merge) {
    probds::OddSketch sketch1(1024);
    probds::OddSketch sketch2(1024);
    probds::OddSketch sketch_merged(1024);

    sketch1.insert("apple");
    sketch1.insert("banana");

    sketch2.insert("banana");
    sketch2.insert("cherry");

    sketch_merged.insert("apple");
    sketch_merged.insert("banana");
    sketch_merged.insert("banana"); // flips back to 0
    sketch_merged.insert("cherry");

    sketch1.merge(sketch2);

    EXPECT_EQ(sketch1.bits(), sketch_merged.bits());
}

TEST(OddSketchTest, SerializationRoundTrip) {
    probds::OddSketch sketch(2048);
    for (int i = 0; i < 100; ++i) {
        sketch.insert("data_" + std::to_string(i));
    }

    std::stringstream ss;
    sketch.serialize(ss);

    auto deserialized = probds::OddSketch<std::string_view>::deserialize(ss);
    EXPECT_EQ(deserialized.m(), 2048u);
    EXPECT_DOUBLE_EQ(deserialized.cardinality(), sketch.cardinality());
    EXPECT_EQ(deserialized.bits(), sketch.bits());
}
