// =============================================================================
// quotient_filter_test.cpp — Tests for probds::QuotientFilter
// =============================================================================

#include "probds/quotient_filter.hpp"
#include <gtest/gtest.h>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

static std::string member_key(std::size_t i) {
    return "member_" + std::to_string(i);
}

TEST(QuotientFilterTest, EmptyFilter) {
    probds::QuotientFilter qf(1000);
    EXPECT_EQ(qf.size(), 0u);
    EXPECT_FALSE(qf.possibly_contains("anything"));
}

TEST(QuotientFilterTest, NoFalseNegatives) {
    constexpr std::size_t n = 500;
    probds::QuotientFilter qf(n);

    for (std::size_t i = 0; i < n; ++i) {
        qf.insert(member_key(i));
    }

    EXPECT_EQ(qf.size(), n);

    for (std::size_t i = 0; i < n; ++i) {
        EXPECT_TRUE(qf.possibly_contains(member_key(i))) << "Missing item i=" << i;
    }
}

TEST(QuotientFilterTest, DeletionCorrectness) {
    probds::QuotientFilter qf(1000);

    qf.insert("item1");
    qf.insert("item2");
    EXPECT_TRUE(qf.possibly_contains("item1"));
    EXPECT_TRUE(qf.possibly_contains("item2"));

    EXPECT_TRUE(qf.remove("item1"));
    EXPECT_FALSE(qf.possibly_contains("item1"));
    EXPECT_TRUE(qf.possibly_contains("item2"));
    EXPECT_EQ(qf.size(), 1u);

    // Removing non-existent
    EXPECT_FALSE(qf.remove("item1"));
}

TEST(QuotientFilterTest, Serialization) {
    probds::QuotientFilter qf(1000);
    qf.insert("serialize_me");
    qf.insert("another_one");

    std::stringstream ss;
    qf.serialize(ss);

    auto deserialized = probds::QuotientFilter<std::string>::deserialize(ss);
    EXPECT_EQ(deserialized.size(), 2u);
    EXPECT_TRUE(deserialized.possibly_contains("serialize_me"));
    EXPECT_TRUE(deserialized.possibly_contains("another_one"));
    EXPECT_FALSE(deserialized.possibly_contains("not_inserted"));
}

TEST(QuotientFilterTest, SetOperations) {
    probds::QuotientFilter qf1(1000);
    probds::QuotientFilter qf2(1000);

    qf1.insert("shared");
    qf1.insert("only1");

    qf2.insert("shared");
    qf2.insert("only2");

    auto qf_union = qf1 | qf2;
    EXPECT_TRUE(qf_union.possibly_contains("shared"));
    EXPECT_TRUE(qf_union.possibly_contains("only1"));
    EXPECT_TRUE(qf_union.possibly_contains("only2"));

    auto qf_intersection = qf1 & qf2;
    EXPECT_TRUE(qf_intersection.possibly_contains("shared"));
    EXPECT_FALSE(qf_intersection.possibly_contains("only1"));
    EXPECT_FALSE(qf_intersection.possibly_contains("only2"));
}

TEST(QuotientFilterTest, BulkInsert) {
    std::vector<std::string> keys = {"bulk1", "bulk2", "bulk3"};
    probds::QuotientFilter qf(1000);
    qf.insert(keys.begin(), keys.end());

    EXPECT_EQ(qf.size(), 3u);
    EXPECT_TRUE(qf.possibly_contains("bulk1"));
    EXPECT_TRUE(qf.possibly_contains("bulk2"));
    EXPECT_TRUE(qf.possibly_contains("bulk3"));
}
