// =============================================================================
// space_saving_test.cpp — Tests for probds::SpaceSaving
// =============================================================================

#include "probds/space_saving.hpp"
#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <vector>

TEST(SpaceSavingTest, BasicInsert) {
    probds::SpaceSaving<std::string> ss(3); // max size = 3

    ss.insert("A");
    ss.insert("B");
    ss.insert("C");

    EXPECT_EQ(ss.estimate("A"), 1u);
    EXPECT_EQ(ss.estimate("B"), 1u);
    EXPECT_EQ(ss.estimate("C"), 1u);

    // Insert new item when full.
    // Minimally counted is A (count 1).
    // A gets replaced by D. New count = 1 + 1 = 2. Error = 1.
    ss.insert("D");
    EXPECT_EQ(ss.estimate("A"), 1u); // un-tracked items estimate is min_count = 1
    EXPECT_EQ(ss.estimate("D"), 2u);
    EXPECT_EQ(ss.error("D"), 1u);
}

TEST(SpaceSavingTest, TopK) {
    probds::SpaceSaving<std::string> ss(5);
    for (int i = 0; i < 100; ++i) ss.insert("A");
    for (int i = 0; i < 50; ++i) ss.insert("B");
    for (int i = 0; i < 10; ++i) ss.insert("C");
    for (int i = 0; i < 2; ++i) ss.insert("D");
    ss.insert("E"); // replaces E or E is inserted because size < 5

    auto top = ss.top_k(3);
    EXPECT_EQ(top.size(), 3u);
    EXPECT_EQ(top[0].first, "A");
    EXPECT_EQ(top[1].first, "B");
    EXPECT_EQ(top[2].first, "C");
}

TEST(SpaceSavingTest, Serialization) {
    probds::SpaceSaving<std::string_view> ss(4);
    ss.insert("one", 5);
    ss.insert("two", 10);
    ss.insert("three", 15);

    std::stringstream stream;
    ss.serialize(stream);

    auto deserialized = probds::SpaceSaving<std::string_view>::deserialize(stream);
    EXPECT_EQ(deserialized.k(), 4u);
    EXPECT_EQ(deserialized.size(), 30u);
    EXPECT_EQ(deserialized.estimate("one"), 5u);
    EXPECT_EQ(deserialized.estimate("two"), 10u);
    EXPECT_EQ(deserialized.estimate("three"), 15u);
}
