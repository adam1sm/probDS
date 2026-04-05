// =============================================================================
// heavy_keeper_test.cpp — Tests for probds::HeavyKeeper
// =============================================================================

#include "probds/heavy_keeper.hpp"
#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <vector>

TEST(HeavyKeeperTest, BasicInsert) {
    // d=4, w=64, k=3
    probds::HeavyKeeper<std::string> hk(4, 64, 3);

    hk.insert("A");
    hk.insert("B");
    hk.insert("C");

    EXPECT_EQ(hk.estimate("A"), 1u);
    EXPECT_EQ(hk.estimate("B"), 1u);
    EXPECT_EQ(hk.estimate("C"), 1u);
}

TEST(HeavyKeeperTest, TopKEviction) {
    // d=4, w=128, k=3
    probds::HeavyKeeper<std::string> hk(4, 128, 3);

    // Insert "A" 10 times, "B" 5 times, "C" 3 times
    for (int i = 0; i < 10; ++i) hk.insert("A");
    for (int i = 0; i < 5; ++i) hk.insert("B");
    for (int i = 0; i < 3; ++i) hk.insert("C");

    // "D" is inserted once. Top-K has capacity 3 and holds A, B, C.
    // "D" has frequency 1, which is not strictly greater than C's count (3),
    // so D is not inserted in Top-K.
    hk.insert("D");
    
    auto top = hk.top_k(3);
    ASSERT_EQ(top.size(), 3u);
    EXPECT_EQ(top[0].first, "A");
    EXPECT_EQ(top[0].second, 10u);
    EXPECT_EQ(top[1].first, "B");
    EXPECT_EQ(top[1].second, 5u);
    EXPECT_EQ(top[2].first, "C");
    EXPECT_EQ(top[2].second, 3u);

    // Now insert "D" 5 more times. Total D count will reach 6, which is > C's count (3).
    // D should evict C and enter Top-K.
    for (int i = 0; i < 5; ++i) hk.insert("D");

    top = hk.top_k(3);
    ASSERT_EQ(top.size(), 3u);
    EXPECT_EQ(top[0].first, "A");
    EXPECT_EQ(top[1].first, "D");
    EXPECT_EQ(top[2].first, "B");
}

TEST(HeavyKeeperTest, SerializationRoundTrip) {
    probds::HeavyKeeper<std::string_view> hk(4, 64, 4);
    hk.insert("one");
    hk.insert("two");
    hk.insert("two");
    hk.insert("three");

    std::stringstream stream;
    hk.serialize(stream);

    auto deserialized = probds::HeavyKeeper<std::string_view>::deserialize(stream);
    EXPECT_EQ(deserialized.d(), 4u);
    EXPECT_EQ(deserialized.w(), 64u);
    EXPECT_EQ(deserialized.k(), 4u);

    EXPECT_EQ(deserialized.estimate("one"), hk.estimate("one"));
    EXPECT_EQ(deserialized.estimate("two"), hk.estimate("two"));
    EXPECT_EQ(deserialized.estimate("three"), hk.estimate("three"));

    auto original_top = hk.top_k(3);
    auto deserialized_top = deserialized.top_k(3);
    ASSERT_EQ(original_top.size(), deserialized_top.size());
    for (size_t i = 0; i < original_top.size(); ++i) {
        EXPECT_EQ(original_top[i].first, deserialized_top[i].first);
        EXPECT_EQ(original_top[i].second, deserialized_top[i].second);
    }
}
