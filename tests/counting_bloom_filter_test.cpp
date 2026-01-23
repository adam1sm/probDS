// =============================================================================
// counting_bloom_filter_test.cpp — Tests for probds::CountingBloomFilter
// =============================================================================

#include "probds/counting_bloom_filter.hpp"
#include <gtest/gtest.h>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

static std::string member_key(std::size_t i) {
    return "member_" + std::to_string(i);
}

static std::string non_member_key(std::size_t i) {
    return "nonmember_" + std::to_string(i);
}

TEST(CountingBloomFilterTest, EmptyFilter) {
    probds::CountingBloomFilter cbf(1000, 0.01);
    EXPECT_EQ(cbf.size(), 0u);
    EXPECT_FALSE(cbf.possibly_contains("anything"));
}

TEST(CountingBloomFilterTest, NoFalseNegatives) {
    constexpr std::size_t n = 1000;
    probds::CountingBloomFilter cbf(n, 0.01);

    for (std::size_t i = 0; i < n; ++i) {
        cbf.insert(member_key(i));
    }

    EXPECT_EQ(cbf.size(), n);

    for (std::size_t i = 0; i < n; ++i) {
        EXPECT_TRUE(cbf.possibly_contains(member_key(i)));
    }
}

TEST(CountingBloomFilterTest, DeletionCorrectness) {
    probds::CountingBloomFilter cbf(1000, 0.01);

    cbf.insert("item1");
    cbf.insert("item2");
    EXPECT_TRUE(cbf.possibly_contains("item1"));
    EXPECT_TRUE(cbf.possibly_contains("item2"));

    EXPECT_TRUE(cbf.remove("item1"));
    EXPECT_FALSE(cbf.possibly_contains("item1"));
    EXPECT_TRUE(cbf.possibly_contains("item2"));
    EXPECT_EQ(cbf.size(), 1u);

    // Removing non-existent
    EXPECT_FALSE(cbf.remove("item1"));
}

TEST(CountingBloomFilterTest, Serialization) {
    probds::CountingBloomFilter cbf(1000, 0.01);
    cbf.insert("serialize_me");
    cbf.insert("another_one");

    std::stringstream ss;
    cbf.serialize(ss);

    auto deserialized = probds::CountingBloomFilter<std::string>::deserialize(ss);
    EXPECT_EQ(deserialized.size(), 2u);
    EXPECT_TRUE(deserialized.possibly_contains("serialize_me"));
    EXPECT_TRUE(deserialized.possibly_contains("another_one"));
    EXPECT_FALSE(deserialized.possibly_contains("not_inserted"));
}

TEST(CountingBloomFilterTest, SetOperations) {
    probds::CountingBloomFilter cbf1(1000, 0.01);
    probds::CountingBloomFilter cbf2(1000, 0.01);

    cbf1.insert("shared");
    cbf1.insert("only1");

    cbf2.insert("shared");
    cbf2.insert("only2");

    auto cbf_union = cbf1 | cbf2;
    EXPECT_TRUE(cbf_union.possibly_contains("shared"));
    EXPECT_TRUE(cbf_union.possibly_contains("only1"));
    EXPECT_TRUE(cbf_union.possibly_contains("only2"));

    auto cbf_intersection = cbf1 & cbf2;
    EXPECT_TRUE(cbf_intersection.possibly_contains("shared"));
    EXPECT_FALSE(cbf_intersection.possibly_contains("only1"));
    EXPECT_FALSE(cbf_intersection.possibly_contains("only2"));
}

TEST(CountingBloomFilterTest, BulkInsert) {
    std::vector<std::string> keys = {"bulk1", "bulk2", "bulk3"};
    probds::CountingBloomFilter cbf(1000, 0.01);
    cbf.insert(keys.begin(), keys.end());

    EXPECT_EQ(cbf.size(), 3u);
    EXPECT_TRUE(cbf.possibly_contains("bulk1"));
    EXPECT_TRUE(cbf.possibly_contains("bulk2"));
    EXPECT_TRUE(cbf.possibly_contains("bulk3"));
}
