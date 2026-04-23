// =============================================================================
// kll_sketch_test.cpp — Tests for probds::KLLSketch
// =============================================================================

#include "probds/kll_sketch.hpp"
#include <gtest/gtest.h>
#include <random>
#include <sstream>
#include <vector>

TEST(KLLSketchTest, EmptyAndSingle) {
    probds::KLLSketch kll(100);
    EXPECT_TRUE(std::isnan(kll.get_quantile(0.5)));

    kll.insert(10.0);
    EXPECT_DOUBLE_EQ(kll.min(), 10.0);
    EXPECT_DOUBLE_EQ(kll.max(), 10.0);
    EXPECT_DOUBLE_EQ(kll.get_quantile(0.0), 10.0);
    EXPECT_DOUBLE_EQ(kll.get_quantile(0.5), 10.0);
    EXPECT_DOUBLE_EQ(kll.get_quantile(1.0), 10.0);
}

TEST(KLLSketchTest, StreamQuantiles) {
    probds::KLLSketch kll(100);
    std::vector<double> vals;
    std::mt19937 gen(123);
    std::normal_distribution<double> dis(50.0, 10.0);

    for (int i = 0; i < 10000; ++i) {
        double v = dis(gen);
        kll.insert(v);
        vals.push_back(v);
    }

    std::sort(vals.begin(), vals.end());

    // Median check
    double expected_median = vals[vals.size() / 2];
    double got_median = kll.get_quantile(0.5);
    // KLL quantile error is bounded by ~O(1/k) -> for k=100, error <= 2-3%
    EXPECT_NEAR(got_median, expected_median, 1.5);

    // 90th percentile check
    double expected_p90 = vals[static_cast<std::size_t>(0.9 * vals.size())];
    double got_p90 = kll.get_quantile(0.9);
    EXPECT_NEAR(got_p90, expected_p90, 1.5);
}

TEST(KLLSketchTest, Merge) {
    probds::KLLSketch kll1(100);
    probds::KLLSketch kll2(100);

    for (int i = 0; i < 5000; ++i) {
        kll1.insert(i);
        kll2.insert(i + 5000);
    }

    kll1.merge(kll2);
    EXPECT_EQ(kll1.size(), 10000u);
    EXPECT_DOUBLE_EQ(kll1.min(), 0.0);
    EXPECT_DOUBLE_EQ(kll1.max(), 9999.0);
    EXPECT_NEAR(kll1.get_quantile(0.5), 5000.0, 150.0);
}

TEST(KLLSketchTest, Serialization) {
    probds::KLLSketch kll(120);
    for (int i = 0; i < 500; ++i) {
        kll.insert(i * 3.14);
    }

    std::stringstream ss;
    kll.serialize(ss);

    auto deserialized = probds::KLLSketch::deserialize(ss);
    EXPECT_EQ(deserialized.k(), 120u);
    EXPECT_EQ(deserialized.size(), 500u);
    EXPECT_DOUBLE_EQ(deserialized.min(), kll.min());
    EXPECT_DOUBLE_EQ(deserialized.max(), kll.max());
    EXPECT_NEAR(deserialized.get_quantile(0.5), kll.get_quantile(0.5), 1e-5);
}

TEST(KLLSketchTest, RepeatedQuantileQueries) {
    probds::KLLSketch kll(100);
    for (int i = 0; i < 100; ++i) {
        kll.insert(static_cast<double>(i));
    }

    double q1 = kll.get_quantile(0.25);
    double q2 = kll.get_quantile(0.25);
    EXPECT_DOUBLE_EQ(q1, q2);

    double q3 = kll.get_quantile(0.75);
    double q4 = kll.get_quantile(0.75);
    EXPECT_DOUBLE_EQ(q3, q4);

    // Modify sketch
    kll.insert(50.0);
    double q5 = kll.get_quantile(0.5);
    double q6 = kll.get_quantile(0.5);
    EXPECT_DOUBLE_EQ(q5, q6);
}

