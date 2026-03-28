// =============================================================================
// count_min_log_test.cpp — Tests for probds::CountMinLog
// =============================================================================

#include "probds/count_min_log.hpp"
#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <vector>
#include <cmath>

TEST(CountMinLogTest, BasicUsage4Bit) {
    probds::CountMinLog<std::string, 4> sketch(0.01, 0.01);
    EXPECT_EQ(sketch.total_count(), 0u);
    EXPECT_EQ(sketch.estimate("hello"), 0u);

    sketch.insert("hello", 5);
    sketch.insert("world", 100);

    EXPECT_EQ(sketch.total_count(), 105u);
    // Logarithmic counters are approximate; we check that estimates are close
    EXPECT_NEAR(static_cast<double>(sketch.estimate("hello")), 5.0, 3.0);
    EXPECT_NEAR(static_cast<double>(sketch.estimate("world")), 100.0, 30.0);
}

TEST(CountMinLogTest, BasicUsage8Bit) {
    probds::CountMinLog<std::string, 8> sketch(0.01, 0.01);
    EXPECT_EQ(sketch.total_count(), 0u);

    sketch.insert("hello", 15);
    sketch.insert("world", 500);

    EXPECT_EQ(sketch.total_count(), 515u);
    EXPECT_NEAR(static_cast<double>(sketch.estimate("hello")), 15.0, 5.0);
    EXPECT_NEAR(static_cast<double>(sketch.estimate("world")), 500.0, 80.0);
}

TEST(CountMinLogTest, SerializationRoundTrip) {
    probds::CountMinLog<std::string, 4> sketch(0.05, 0.05);
    sketch.insert("item1", 10);
    sketch.insert("item2", 50);

    std::stringstream ss;
    sketch.serialize(ss);

    auto sketch2 = probds::CountMinLog<std::string, 4>::deserialize(ss);
    EXPECT_EQ(sketch2.total_count(), 60u);
    EXPECT_EQ(sketch2.width(), sketch.width());
    EXPECT_EQ(sketch2.depth(), sketch.depth());
    EXPECT_NEAR(static_cast<double>(sketch2.estimate("item1")), static_cast<double>(sketch.estimate("item1")), 1.0);
    EXPECT_NEAR(static_cast<double>(sketch2.estimate("item2")), static_cast<double>(sketch.estimate("item2")), 1.0);
}
