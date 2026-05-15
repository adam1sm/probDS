// =============================================================================
// params_test.cpp — Tests for probds::params estimators and recommendation
// =============================================================================

#include "probds/params.hpp"
#include <gtest/gtest.h>

TEST(ParamsTest, BloomEstimator) {
    auto bp = probds::bloom_params(10000, 0.01);
    EXPECT_GT(bp.m, 0u);
    EXPECT_GT(bp.k, 0u);
    EXPECT_EQ(bp.memory_bytes, bp.m / 8);
    // Check that it rounded to a power of 2
    EXPECT_EQ((bp.m & (bp.m - 1)), 0u);
}

TEST(ParamsTest, CuckooEstimator) {
    auto cp = probds::cuckoo_params(10000, 0.01);
    EXPECT_GT(cp.capacity, 10000u);
    EXPECT_EQ(cp.bucket_size, 4u);
    EXPECT_GE(cp.fingerprint_bits, 4u);
    EXPECT_LE(cp.fingerprint_bits, 8u);
    EXPECT_EQ(cp.memory_bytes, cp.capacity * sizeof(std::uint8_t));
}

TEST(ParamsTest, HllEstimator) {
    auto hp = probds::hll_params(0.01);
    EXPECT_GE(hp.precision, 4u);
    EXPECT_LE(hp.precision, 16u);
    EXPECT_EQ(hp.memory_bytes, std::size_t{1} << hp.precision);
}

TEST(ParamsTest, CmsEstimator) {
    auto cp = probds::cms_params(0.01, 0.01);
    EXPECT_GT(cp.width, 0u);
    EXPECT_GT(cp.depth, 0u);
    EXPECT_EQ((cp.width & (cp.width - 1)), 0u);
    EXPECT_EQ(cp.memory_bytes, cp.depth * cp.width * sizeof(std::uint64_t));
}

TEST(ParamsTest, RecommendationEngine) {
    // n = 10000, fpr = 0.01, budget = 20000 bytes (which fits RibbonFilter, XorFilter, BloomFilter, etc.)
    auto rec = probds::recommend(10000, 0.01, 20000);
    EXPECT_FALSE(rec.structure_name.empty());
    EXPECT_LE(rec.memory_bytes, 20000u);
    EXPECT_GT(rec.expected_fpr, 0.0);

    // Tight budget = 9000 bytes (fits BloomFilter (8192 bytes) but not Cuckoo/Xor)
    auto rec_tight = probds::recommend(10000, 0.05, 9000);
    EXPECT_FALSE(rec_tight.structure_name.empty());
    EXPECT_LE(rec_tight.memory_bytes, 9000u);
}
