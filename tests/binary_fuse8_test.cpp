// =============================================================================
// binary_fuse8_test.cpp — Tests for probds::BinaryFuse8
// =============================================================================

#include "probds/binary_fuse8.hpp"
#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <vector>

TEST(BinaryFuse8Test, EmptyFilter) {
    probds::BinaryFuse8<std::string> filter;
    EXPECT_EQ(filter.size(), 0u);
    EXPECT_FALSE(filter.possibly_contains("test"));
}

TEST(BinaryFuse8Test, SingleElement) {
    std::vector<std::string> keys = {"hello"};
    probds::BinaryFuse8<std::string> filter(keys.begin(), keys.end());
    
    EXPECT_EQ(filter.size(), 1u);
    EXPECT_TRUE(filter.possibly_contains("hello"));
    EXPECT_FALSE(filter.possibly_contains("world"));
}

TEST(BinaryFuse8Test, BasicContainment) {
    std::vector<std::string> keys = {"one", "two", "three", "four", "five"};
    probds::BinaryFuse8<std::string> filter(keys.begin(), keys.end());

    EXPECT_EQ(filter.size(), 5u);

    // No false negatives
    for (const auto& key : keys) {
        EXPECT_TRUE(filter.possibly_contains(key));
    }
}

TEST(BinaryFuse8Test, FalsePositiveRate) {
    // Populate with 10,000 integers
    std::vector<std::uint64_t> keys;
    for (std::uint64_t i = 0; i < 10000; ++i) {
        keys.push_back(i * 2); // even numbers
    }

    probds::BinaryFuse8<std::uint64_t> filter(keys.begin(), keys.end());

    // Verify all present keys match
    for (auto key : keys) {
        ASSERT_TRUE(filter.possibly_contains(key));
    }

    // Query 10,000 non-present keys (odd numbers)
    std::size_t false_positives = 0;
    for (std::uint64_t i = 0; i < 10000; ++i) {
        if (filter.possibly_contains(i * 2 + 1)) {
            false_positives++;
        }
    }

    double fpr = static_cast<double>(false_positives) / 10000.0;
    // Expected false positive rate of 8-bit fingerprint is ~1/256 (0.39%)
    // Let's assert it is well under 1.0% (with some statistical buffer)
    EXPECT_LT(fpr, 0.01);
}

TEST(BinaryFuse8Test, SerializationRoundTrip) {
    std::vector<std::string> keys = {"alice", "bob", "charlie"};
    probds::BinaryFuse8<std::string> filter(keys.begin(), keys.end());

    std::stringstream ss;
    filter.serialize(ss);

    auto filter2 = probds::BinaryFuse8<std::string>::deserialize(ss);
    EXPECT_EQ(filter2.size(), 3u);
    EXPECT_TRUE(filter2.possibly_contains("alice"));
    EXPECT_TRUE(filter2.possibly_contains("bob"));
    EXPECT_TRUE(filter2.possibly_contains("charlie"));
    EXPECT_FALSE(filter2.possibly_contains("david"));
}
