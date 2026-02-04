// =============================================================================
// blocked_bloom_filter_test.cpp — Tests for probds::BlockedBloomFilter
// =============================================================================

#include "probds/blocked_bloom_filter.hpp"
#include <gtest/gtest.h>
#include <cmath>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

static std::string member_key(std::size_t i) {
    return "member_" + std::to_string(i);
}

TEST(BlockedBloomFilterTest, EmptyFilter) {
    probds::BlockedBloomFilter bbf(1000, 0.01);
    EXPECT_EQ(bbf.size(), 0u);
    EXPECT_FALSE(bbf.possibly_contains("anything"));
}

TEST(BlockedBloomFilterTest, Alignment) {
    probds::BlockedBloomFilter bbf(1000, 0.01);
    EXPECT_GT(bbf.block_count(), 0u);
    EXPECT_EQ(bbf.memory_bytes() % 64, 0u) << "Memory bytes is not cache line aligned";
}

TEST(BlockedBloomFilterTest, NoFalseNegatives) {
    constexpr std::size_t n = 1000;
    probds::BlockedBloomFilter bbf(n, 0.01);

    for (std::size_t i = 0; i < n; ++i) {
        bbf.insert(member_key(i));
    }

    EXPECT_EQ(bbf.size(), n);

    for (std::size_t i = 0; i < n; ++i) {
        EXPECT_TRUE(bbf.possibly_contains(member_key(i)));
    }
}

TEST(BlockedBloomFilterTest, BulkLookupMatches) {
    constexpr std::size_t n = 500;
    probds::BlockedBloomFilter bbf(n, 0.01);

    std::vector<std::string> key_storage;
    for (std::size_t i = 0; i < n; ++i) {
        bbf.insert(member_key(i));
        key_storage.push_back(member_key(i));
    }

    // Add some non-members
    for (std::size_t i = 0; i < n; ++i) {
        key_storage.push_back("nonmember_" + std::to_string(i));
    }

    std::vector<std::string_view> keys;
    keys.reserve(key_storage.size());
    for (const auto& k : key_storage) {
        keys.push_back(k);
    }

    auto bulk_results = std::make_unique<bool[]>(keys.size());
    bbf.possibly_contains_bulk(keys.data(), bulk_results.get(), keys.size());

    for (std::size_t i = 0; i < keys.size(); ++i) {
        const bool expected = bbf.possibly_contains(keys[i]);
        EXPECT_EQ(bulk_results[i], expected) << "Mismatch at index i=" << i << " key=" << keys[i];
    }
}

TEST(BlockedBloomFilterTest, Serialization) {
    probds::BlockedBloomFilter bbf(1000, 0.01);
    bbf.insert("serialize_me");
    bbf.insert("another_one");

    std::stringstream ss;
    bbf.serialize(ss);

    auto deserialized = probds::BlockedBloomFilter<std::string>::deserialize(ss);
    EXPECT_EQ(deserialized.size(), 2u);
    EXPECT_TRUE(deserialized.possibly_contains("serialize_me"));
    EXPECT_TRUE(deserialized.possibly_contains("another_one"));
    EXPECT_FALSE(deserialized.possibly_contains("not_inserted"));
}

TEST(BlockedBloomFilterTest, SetOperations) {
    probds::BlockedBloomFilter bbf1(1000, 0.01);
    probds::BlockedBloomFilter bbf2(1000, 0.01);

    bbf1.insert("shared");
    bbf1.insert("only1");

    bbf2.insert("shared");
    bbf2.insert("only2");

    auto bbf_union = bbf1 | bbf2;
    EXPECT_TRUE(bbf_union.possibly_contains("shared"));
    EXPECT_TRUE(bbf_union.possibly_contains("only1"));
    EXPECT_TRUE(bbf_union.possibly_contains("only2"));

    auto bbf_intersection = bbf1 & bbf2;
    EXPECT_TRUE(bbf_intersection.possibly_contains("shared"));
    EXPECT_FALSE(bbf_intersection.possibly_contains("only1"));
    EXPECT_FALSE(bbf_intersection.possibly_contains("only2"));
}

