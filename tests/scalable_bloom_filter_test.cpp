// =============================================================================
// scalable_bloom_filter_test.cpp — Tests for probds::ScalableBloomFilter
// =============================================================================

#include "probds/scalable_bloom_filter.hpp"
#include <gtest/gtest.h>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

static std::string member_key(std::size_t i) {
    return "member_" + std::to_string(i);
}

TEST(ScalableBloomFilterTest, EmptyFilter) {
    probds::ScalableBloomFilter sbf(100, 0.01);
    EXPECT_EQ(sbf.size(), 0u);
    EXPECT_EQ(sbf.stage_count(), 1u);
    EXPECT_FALSE(sbf.possibly_contains("anything"));
}

TEST(ScalableBloomFilterTest, DynamicGrowth) {
    // Stage capacity is 100. We insert 350 items.
    // It should grow to at least 3 stages (100 + 200 + 400 capacity bounds)
    constexpr std::size_t n = 350;
    probds::ScalableBloomFilter sbf(100, 0.01, 2.0, 0.8);

    for (std::size_t i = 0; i < n; ++i) {
        sbf.insert(member_key(i));
    }

    EXPECT_EQ(sbf.size(), n);
    EXPECT_GE(sbf.stage_count(), 2u);

    // Verify all inserted items are present
    for (std::size_t i = 0; i < n; ++i) {
        EXPECT_TRUE(sbf.possibly_contains(member_key(i))) << "Missing item i=" << i;
    }
}

TEST(ScalableBloomFilterTest, FalsePositiveRate) {
    constexpr std::size_t n = 2000;
    constexpr double target_fpr = 0.01;
    probds::ScalableBloomFilter sbf(100, target_fpr, 2.0, 0.5);

    for (std::size_t i = 0; i < n; ++i) {
        sbf.insert(member_key(i));
    }

    // Query 10K non-members
    std::size_t false_positives = 0;
    for (std::size_t i = 0; i < 10000; ++i) {
        if (sbf.possibly_contains("nonmember_" + std::to_string(i))) {
            ++false_positives;
        }
    }

    const double observed_fpr = static_cast<double>(false_positives) / 10000.0;
    // Observed FPR should be bounded by target_fpr (allow some variance)
    EXPECT_LE(observed_fpr, target_fpr * 3.0) << "Observed FPR " << observed_fpr << " exceeds target bounds";
}

TEST(ScalableBloomFilterTest, Serialization) {
    probds::ScalableBloomFilter sbf(100, 0.01);
    for (int i = 0; i < 250; ++i) {
        sbf.insert(member_key(i));
    }

    std::stringstream ss;
    sbf.serialize(ss);

    auto deserialized = probds::ScalableBloomFilter<std::string>::deserialize(ss);
    EXPECT_EQ(deserialized.size(), 250u);
    EXPECT_GE(deserialized.stage_count(), 2u);

    for (int i = 0; i < 250; ++i) {
        EXPECT_TRUE(deserialized.possibly_contains(member_key(i)));
    }
}

TEST(ScalableBloomFilterTest, SetOperations) {
    probds::ScalableBloomFilter sbf1(100, 0.01);
    probds::ScalableBloomFilter sbf2(100, 0.01);

    sbf1.insert("shared");
    sbf1.insert("only1");

    sbf2.insert("shared");
    sbf2.insert("only2");

    auto sbf_union = sbf1 | sbf2;
    EXPECT_TRUE(sbf_union.possibly_contains("shared"));
    EXPECT_TRUE(sbf_union.possibly_contains("only1"));
    EXPECT_TRUE(sbf_union.possibly_contains("only2"));

    auto sbf_intersection = sbf1 & sbf2;
    EXPECT_TRUE(sbf_intersection.possibly_contains("shared"));
    EXPECT_FALSE(sbf_intersection.possibly_contains("only1"));
    EXPECT_FALSE(sbf_intersection.possibly_contains("only2"));
}
