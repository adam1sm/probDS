// =============================================================================
// tdigest_test.cpp — Tests for probds::tDigest
// =============================================================================

#include "probds/tdigest.hpp"
#include <gtest/gtest.h>
#include <random>
#include <sstream>
#include <vector>

TEST(tDigestTest, EmptyAndSingle) {
    probds::tDigest td(100.0);
    EXPECT_TRUE(std::isnan(td.quantile(0.5)));
    EXPECT_TRUE(std::isnan(td.cdf(0.5)));

    td.insert(42.0);
    EXPECT_DOUBLE_EQ(td.min(), 42.0);
    EXPECT_DOUBLE_EQ(td.max(), 42.0);
    EXPECT_DOUBLE_EQ(td.quantile(0.0), 42.0);
    EXPECT_DOUBLE_EQ(td.quantile(0.5), 42.0);
    EXPECT_DOUBLE_EQ(td.quantile(1.0), 42.0);
    EXPECT_DOUBLE_EQ(td.cdf(42.0), 0.5); // single element center
}

TEST(tDigestTest, UniformDistributionQuantiles) {
    probds::tDigest td(100.0);
    std::vector<double> vals;
    std::mt19937 gen(42);
    std::uniform_real_distribution<double> dis(0.0, 100.0);

    for (int i = 0; i < 5000; ++i) {
        double v = dis(gen);
        td.insert(v);
        vals.push_back(v);
    }

    std::sort(vals.begin(), vals.end());

    // Check percentiles: 10th, 50th, 90th
    for (double q : {0.1, 0.5, 0.9}) {
        double expected = vals[static_cast<std::size_t>(q * vals.size())];
        double got = td.quantile(q);
        // Standard t-Digest error at compression=100 is typically < 1%
        EXPECT_NEAR(got, expected, 2.0); 
    }
}

TEST(tDigestTest, Merge) {
    probds::tDigest td1(100.0);
    probds::tDigest td2(100.0);

    for (int i = 0; i < 1000; ++i) {
        td1.insert(i);
        td2.insert(i + 1000);
    }

    td1.merge(td2);
    EXPECT_EQ(td1.total_weight(), 2000.0);
    EXPECT_DOUBLE_EQ(td1.min(), 0.0);
    EXPECT_DOUBLE_EQ(td1.max(), 1999.0);
    EXPECT_NEAR(td1.quantile(0.5), 1000.0, 10.0);
}

TEST(tDigestTest, Serialization) {
    probds::tDigest td(50.0);
    for (int i = 0; i < 200; ++i) {
        td.insert(i * 1.5);
    }

    std::stringstream ss;
    td.serialize(ss);

    auto deserialized = probds::tDigest::deserialize(ss);
    EXPECT_DOUBLE_EQ(deserialized.compression(), 50.0);
    EXPECT_DOUBLE_EQ(deserialized.total_weight(), 200.0);
    EXPECT_DOUBLE_EQ(deserialized.min(), td.min());
    EXPECT_DOUBLE_EQ(deserialized.max(), td.max());
    EXPECT_NEAR(deserialized.quantile(0.5), td.quantile(0.5), 1e-5);
}
