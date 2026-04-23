// =============================================================================
// dd_sketch_test.cpp — Tests for probds::DDSketch
// =============================================================================

#include "probds/dd_sketch.hpp"
#include <gtest/gtest.h>
#include <sstream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <random>

TEST(DDSketchTest, EmptySketch) {
    probds::DDSketch sketch(0.01);
    EXPECT_EQ(sketch.size(), 0u);
    EXPECT_TRUE(std::isnan(sketch.get_quantile(0.5)));
}

TEST(DDSketchTest, ZeroAndBasicValues) {
    probds::DDSketch sketch(0.01);
    sketch.insert(0.0);
    sketch.insert(0.0);
    EXPECT_EQ(sketch.size(), 2u);
    EXPECT_EQ(sketch.zero_count(), 2u);
    EXPECT_DOUBLE_EQ(sketch.get_quantile(0.5), 0.0);
}

TEST(DDSketchTest, RelativeErrorGuarantee) {
    // 1% relative error target
    double alpha = 0.01;
    probds::DDSketch sketch(alpha);

    // Insert positive values (geometric progression to test mapping boundary)
    std::vector<double> values;
    std::mt19937 gen(42);
    std::uniform_real_distribution<double> dist(1.0, 10000.0);

    for (int i = 0; i < 1000; ++i) {
        double v = dist(gen);
        values.push_back(v);
        sketch.insert(v);
    }

    std::sort(values.begin(), values.end());

    // Test multiple quantiles
    std::vector<double> quantiles = {0.01, 0.1, 0.25, 0.5, 0.75, 0.9, 0.99};
    for (double q : quantiles) {
        double estimate = sketch.get_quantile(q);
        std::size_t rank = static_cast<std::size_t>(q * (values.size() - 1));
        double exact = values[rank];

        double rel_error = std::abs(estimate - exact) / exact;
        EXPECT_LE(rel_error, alpha + 1e-9) << "Fails at quantile " << q 
                                           << " (Estimate: " << estimate 
                                           << ", Exact: " << exact 
                                           << ", RelError: " << rel_error << ")";
    }
}

TEST(DDSketchTest, NegativeValues) {
    double alpha = 0.02;
    probds::DDSketch sketch(alpha);

    // Insert negative values
    std::vector<double> values;
    std::mt19937 gen(123);
    std::uniform_real_distribution<double> dist(-5000.0, -10.0);

    for (int i = 0; i < 500; ++i) {
        double v = dist(gen);
        values.push_back(v);
        sketch.insert(v);
    }

    std::sort(values.begin(), values.end());

    std::vector<double> quantiles = {0.1, 0.3, 0.5, 0.7, 0.9};
    for (double q : quantiles) {
        double estimate = sketch.get_quantile(q);
        std::size_t rank = static_cast<std::size_t>(q * (values.size() - 1));
        double exact = values[rank];

        double rel_error = std::abs(estimate - exact) / std::abs(exact);
        EXPECT_LE(rel_error, alpha + 1e-9) << "Fails at quantile " << q 
                                           << " (Estimate: " << estimate 
                                           << ", Exact: " << exact 
                                           << ", RelError: " << rel_error << ")";
    }
}

TEST(DDSketchTest, MergeSketches) {
    double alpha = 0.01;
    probds::DDSketch s1(alpha);
    probds::DDSketch s2(alpha);

    s1.insert(10.0);
    s1.insert(20.0);
    s2.insert(30.0);
    s2.insert(40.0);

    s1.merge(s2);
    EXPECT_EQ(s1.size(), 4u);

    // Test quantiles on merged set: {10, 20, 30, 40}
    // quantile 0.25 -> 10.0
    // quantile 0.50 -> 20.0
    // quantile 0.75 -> 30.0
    // quantile 1.00 -> 40.0
    EXPECT_NEAR(s1.get_quantile(0.25), 10.0, 10.0 * 2 * alpha);
    EXPECT_NEAR(s1.get_quantile(0.50), 20.0, 20.0 * 2 * alpha);
    EXPECT_NEAR(s1.get_quantile(0.75), 30.0, 30.0 * 2 * alpha);
    EXPECT_NEAR(s1.get_quantile(1.00), 40.0, 40.0 * 2 * alpha);
}

TEST(DDSketchTest, SerializationRoundTrip) {
    probds::DDSketch sketch(0.015);
    sketch.insert(-100.0);
    sketch.insert(0.0);
    sketch.insert(500.0);
    sketch.insert(1000.0);

    std::stringstream ss;
    sketch.serialize(ss);

    auto deserialized = probds::DDSketch::deserialize(ss);
    EXPECT_DOUBLE_EQ(deserialized.alpha(), 0.015);
    EXPECT_EQ(deserialized.size(), 4u);
    EXPECT_EQ(deserialized.zero_count(), 1u);

    EXPECT_DOUBLE_EQ(deserialized.get_quantile(0.0), sketch.get_quantile(0.0));
    EXPECT_DOUBLE_EQ(deserialized.get_quantile(0.25), sketch.get_quantile(0.25));
    EXPECT_DOUBLE_EQ(deserialized.get_quantile(0.5), sketch.get_quantile(0.5));
    EXPECT_DOUBLE_EQ(deserialized.get_quantile(0.75), sketch.get_quantile(0.75));
    EXPECT_DOUBLE_EQ(deserialized.get_quantile(1.0), sketch.get_quantile(1.0));
}
