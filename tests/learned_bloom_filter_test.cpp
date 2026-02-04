// =============================================================================
// learned_bloom_filter_test.cpp — Tests for probds::LearnedBloomFilter
// =============================================================================

#include "probds/learned_bloom_filter.hpp"
#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <vector>

TEST(LearnedBloomFilterTest, BasicOperations) {
    // Mock classifier: returns true for keys containing "even"
    auto classifier = [](const std::string& key) {
        return key.find("even") != std::string::npos;
    };

    // Expected false negatives = 10
    probds::LearnedBloomFilter<std::string, decltype(classifier)> filter(classifier, 0.01, 10);

    // Insert keys: some classified positive, some classified negative
    // "apple_even" -> classifier positive
    // "banana_odd" -> classifier negative (will go to backup bloom)
    filter.insert("apple_even");
    filter.insert("banana_odd");

    EXPECT_EQ(filter.size(), 2u);

    // Query keys
    // Classified positive keys must return true
    EXPECT_TRUE(filter.possibly_contains("apple_even"));
    EXPECT_TRUE(filter.possibly_contains("cherry_even")); // true because of classifier positive (false positive)

    // Classified negative keys that WERE inserted must return true
    EXPECT_TRUE(filter.possibly_contains("banana_odd")); // in backup bloom

    // Classified negative keys that WERE NOT inserted must return false (with high probability)
    EXPECT_FALSE(filter.possibly_contains("orange_odd"));
}

TEST(LearnedBloomFilterTest, SerializationRoundTrip) {
    auto classifier = [](const std::string& key) {
        return key.length() % 2 == 0;
    };

    probds::LearnedBloomFilter<std::string, decltype(classifier)> filter(classifier, 0.01, 100);
    filter.insert("aa"); // length 2 (positive)
    filter.insert("bbb"); // length 3 (negative, backup)

    std::stringstream ss;
    filter.serialize(ss);

    auto deserialized = probds::LearnedBloomFilter<std::string, decltype(classifier)>::deserialize(ss, classifier);
    
    EXPECT_TRUE(deserialized.possibly_contains("aa"));
    EXPECT_TRUE(deserialized.possibly_contains("bbb"));
    EXPECT_FALSE(deserialized.possibly_contains("ccccc"));
}
