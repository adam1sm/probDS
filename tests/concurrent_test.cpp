// =============================================================================
// concurrent_test.cpp — Tests for concurrent structures
// =============================================================================

#include "probds/concurrent.hpp"
#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <string>

TEST(ConcurrentTest, BloomFilterMultithreaded) {
    // sizing: expected insertions = 8000
    probds::ConcurrentBloomFilter<std::string> filter(8000, 0.01, 128);

    const int num_threads = 8;
    const int inserts_per_thread = 1000;
    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&filter, t]() {
            for (int i = 0; i < inserts_per_thread; ++i) {
                filter.insert("thread_" + std::to_string(t) + "_item_" + std::to_string(i));
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    // Verify all inserted items are present (no false negatives)
    for (int t = 0; t < num_threads; ++t) {
        for (int i = 0; i < inserts_per_thread; ++i) {
            EXPECT_TRUE(filter.possibly_contains("thread_" + std::to_string(t) + "_item_" + std::to_string(i)));
        }
    }
}

TEST(ConcurrentTest, HyperLogLogMultithreaded) {
    probds::ConcurrentHyperLogLog<std::string> hll(14, 64);

    const int num_threads = 8;
    const int inserts_per_thread = 1000;
    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&hll, t]() {
            for (int i = 0; i < inserts_per_thread; ++i) {
                hll.insert("thread_" + std::to_string(t) + "_item_" + std::to_string(i));
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    // Verify estimate is close to 8000
    double estimate = static_cast<double>(hll.estimate());
    EXPECT_NEAR(estimate, 8000.0, 8000.0 * 3.0 * hll.relative_error());
}

TEST(ConcurrentTest, CountMinMultithreaded) {
    probds::ConcurrentCountMin<std::string> cms(0.01, 0.01);

    const int num_threads = 8;
    const int increments_per_thread = 1000;
    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&cms]() {
            for (int i = 0; i < increments_per_thread; ++i) {
                cms.insert("shared_item", 1);
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    // Estimate should be exactly 8000 since there are no collisions
    EXPECT_EQ(cms.estimate("shared_item"), 8000u);
}
