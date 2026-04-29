// =============================================================================
// reservoir_sampler_test.cpp — Tests for probds::ReservoirSampler
// =============================================================================

#include "probds/reservoir_sampler.hpp"
#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <vector>

TEST(ReservoirSamplerTest, BasicSampling) {
    probds::ReservoirSampler<int> sampler(10);
    EXPECT_EQ(sampler.k(), 10u);
    EXPECT_EQ(sampler.size(), 0u);

    for (int i = 0; i < 5; ++i) {
        sampler.insert(i);
    }
    // Size <= k: should contain all inserted elements
    EXPECT_EQ(sampler.size(), 5u);
    auto samples = sampler.get_samples();
    for (int i = 0; i < 5; ++i) {
        EXPECT_NE(std::find(samples.begin(), samples.end(), i), samples.end());
    }

    for (int i = 5; i < 100; ++i) {
        sampler.insert(i);
    }
    EXPECT_EQ(sampler.size(), 10u);
}

TEST(ReservoirSamplerTest, Merge) {
    probds::ReservoirSampler<int> sampler1(10);
    probds::ReservoirSampler<int> sampler2(10);

    for (int i = 0; i < 50; ++i) {
        sampler1.insert(i);
        sampler2.insert(i + 100);
    }

    sampler1.merge(sampler2);
    EXPECT_EQ(sampler1.size(), 10u);
    EXPECT_EQ(sampler1.total_processed(), 100u);
}

TEST(ReservoirSamplerTest, Serialization) {
    probds::ReservoirSampler<std::string> sampler(5);
    sampler.insert("hello");
    sampler.insert("world");

    std::stringstream ss;
    sampler.serialize(ss);

    auto deserialized = probds::ReservoirSampler<std::string>::deserialize(ss);
    EXPECT_EQ(deserialized.k(), 5u);
    EXPECT_EQ(deserialized.total_processed(), 2u);
    EXPECT_EQ(deserialized.size(), 2u);
    EXPECT_EQ(deserialized.get_samples(), sampler.get_samples());
}
