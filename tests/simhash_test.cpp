// =============================================================================
// simhash_test.cpp — Tests for probds::SimHash
// =============================================================================

#include "probds/simhash.hpp"
#include <gtest/gtest.h>
#include <sstream>
#include <string>

TEST(SimHashTest, EmptyAndBasic) {
    probds::SimHash sh;
    EXPECT_EQ(sh.size(), 0u);
    EXPECT_EQ(sh.get_fingerprint(), 0u);
}

TEST(SimHashTest, SimilarityBounds) {
    probds::SimHash sh1;
    probds::SimHash sh2;

    // Identical
    sh1.insert("doc1");
    sh2.insert("doc1");
    EXPECT_DOUBLE_EQ(sh1.similarity(sh2), 1.0);
    EXPECT_EQ(sh1.hamming_distance(sh2), 0u);

    // Completely different inputs will have Hamming distance around 32 (similarity ~ 0.0)
    probds::SimHash sh3;
    sh3.insert("very_different_document_text_12345");
    
    double sim = sh1.similarity(sh3);
    EXPECT_NEAR(sim, 0.0, 0.6); // Broad bounds due to small feature size (1 insert)
}

TEST(SimHashTest, MergeAndWeights) {
    probds::SimHash sh1;
    sh1.insert("feature1", 5.0);
    sh1.insert("feature2", 1.0);

    probds::SimHash sh2;
    sh2.insert("feature1", 2.0);

    sh2.merge(sh1);
    EXPECT_EQ(sh2.size(), 3u);
}

TEST(SimHashTest, Serialization) {
    probds::SimHash sh;
    sh.insert("lorem");
    sh.insert("ipsum");

    std::stringstream ss;
    sh.serialize(ss);

    auto deserialized = probds::SimHash<std::string_view>::deserialize(ss);
    EXPECT_EQ(deserialized.size(), 2u);
    EXPECT_EQ(deserialized.get_fingerprint(), sh.get_fingerprint());
}
