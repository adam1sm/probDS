// =============================================================================
// misra_gries_test.cpp — Tests for probds::MisraGries
// =============================================================================

#include "probds/misra_gries.hpp"
#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <vector>

TEST(MisraGriesTest, BasicInsert) {
    probds::MisraGries<std::string> mg(4); // max size = 3

    mg.insert("A");
    mg.insert("B");
    mg.insert("A");
    mg.insert("C");

    EXPECT_EQ(mg.estimate("A"), 2u);
    EXPECT_EQ(mg.estimate("B"), 1u);
    EXPECT_EQ(mg.estimate("C"), 1u);
    EXPECT_EQ(mg.estimate("D"), 0u);
}

TEST(MisraGriesTest, DecrementPruning) {
    probds::MisraGries<std::string> mg(3); // max size = 2

    // Insert 2 heavy keys
    mg.insert("A", 10);
    mg.insert("B", 10);
    EXPECT_EQ(mg.counts().size(), 2u);

    // Insert a third key with count 2.
    // Since map is full, and 2 < min_val (10), we decrement A and B by 2.
    // C is not inserted.
    mg.insert("C", 2);
    EXPECT_EQ(mg.estimate("A"), 8u);
    EXPECT_EQ(mg.estimate("B"), 8u);
    EXPECT_EQ(mg.estimate("C"), 0u);

    // Insert D with count 12.
    // Since 12 >= min_val (8), we subtract 8 from all (A and B counts drop to 0 and are deleted),
    // and D is inserted with count 12 - 8 = 4.
    mg.insert("D", 12);
    EXPECT_EQ(mg.estimate("A"), 0u);
    EXPECT_EQ(mg.estimate("B"), 0u);
    EXPECT_EQ(mg.estimate("D"), 4u);
}

TEST(MisraGriesTest, HeavyHitters) {
    probds::MisraGries<std::string> mg(5); // max size = 4
    for (int i = 0; i < 50; ++i) mg.insert("heavy1");
    for (int i = 0; i < 30; ++i) mg.insert("heavy2");
    for (int i = 0; i < 5; ++i) mg.insert("light");

    auto hh = mg.heavy_hitters(20);
    EXPECT_EQ(hh.size(), 2u);
    
    bool found1 = false, found2 = false;
    for (const auto& pair : hh) {
        if (pair.first == "heavy1") found1 = true;
        if (pair.first == "heavy2") found2 = true;
    }
    EXPECT_TRUE(found1);
    EXPECT_TRUE(found2);
}

TEST(MisraGriesTest, Merge) {
    probds::MisraGries<std::string> mg1(5);
    probds::MisraGries<std::string> mg2(5);

    mg1.insert("A", 10);
    mg1.insert("B", 5);

    mg2.insert("A", 5);
    mg2.insert("C", 20);

    mg1.merge(mg2);
    EXPECT_EQ(mg1.estimate("A"), 15u);
    EXPECT_EQ(mg1.estimate("C"), 20u);
}

TEST(MisraGriesTest, Serialization) {
    probds::MisraGries<std::string_view> mg(4);
    mg.insert("apple", 10);
    mg.insert("banana", 15);

    std::stringstream ss;
    mg.serialize(ss);

    auto deserialized = probds::MisraGries<std::string_view>::deserialize(ss);
    EXPECT_EQ(deserialized.k(), 4u);
    EXPECT_EQ(deserialized.size(), 25u);
    EXPECT_EQ(deserialized.estimate("apple"), 10u);
    EXPECT_EQ(deserialized.estimate("banana"), 15u);
}
