// =============================================================================
// exponential_histogram_test.cpp — Tests for probds::ExponentialHistogram
// =============================================================================

#include "probds/exponential_histogram.hpp"
#include <gtest/gtest.h>
#include <sstream>
#include <vector>

TEST(ExponentialHistogramTest, BasicCounting) {
    probds::ExponentialHistogram eh(100, 0.1);
    EXPECT_EQ(eh.estimate_last_n(100), 0u);

    // Update with 50 bits of 1
    for (int i = 0; i < 50; ++i) {
        eh.update(true);
    }
    
    // Exact count when window is fully inside
    EXPECT_EQ(eh.estimate_last_n(50), 50u);
    EXPECT_EQ(eh.estimate_last_n(100), 50u);
}

TEST(ExponentialHistogramTest, SlidingWindowExpiration) {
    // Window size = 20, error = 0.1
    probds::ExponentialHistogram eh(20, 0.1);

    // Add 20 ones
    for (int i = 0; i < 20; ++i) {
        eh.update(true);
    }
    
    // Add 10 zeros
    for (int i = 0; i < 10; ++i) {
        eh.update(false);
    }

    // Out of the last 20 elements: we have 10 zeros and 10 ones.
    // So the exact count of ones in the last 20 elements is 10.
    std::uint64_t est = eh.estimate_last_n(20);
    EXPECT_NEAR(static_cast<double>(est), 10.0, 2.0); // 10% relative error is small, estimate should be close
}

TEST(ExponentialHistogramTest, ErrorGuarantee) {
    // Large window size, multiple ones
    probds::ExponentialHistogram eh(1000, 0.05);
    
    std::vector<bool> stream;
    // Generate a stream of 1000 elements with 50% density
    for (int i = 0; i < 1000; ++i) {
        bool bit = (i % 3 == 0 || i % 7 == 0);
        stream.push_back(bit);
        eh.update(bit);
    }

    // Verify estimates for multiple sub-windows
    std::vector<std::size_t> query_windows = {100, 200, 500, 1000};
    for (auto w : query_windows) {
        std::size_t exact_count = 0;
        for (std::size_t i = 1000 - w; i < 1000; ++i) {
            if (stream[i]) exact_count++;
        }

        std::uint64_t est = eh.estimate_last_n(w);
        if (exact_count == 0) {
            EXPECT_EQ(est, 0u);
        } else {
            double rel_error = std::abs(static_cast<double>(est) - static_cast<double>(exact_count)) / static_cast<double>(exact_count);
            // DGIM guarantee is relative error <= epsilon (with a bit of buffer)
            EXPECT_LE(rel_error, 0.1);
        }
    }
}

TEST(ExponentialHistogramTest, SerializationRoundTrip) {
    probds::ExponentialHistogram eh(50, 0.2);
    for (int i = 0; i < 30; ++i) {
        eh.update(i % 2 == 0);
    }

    std::stringstream ss;
    eh.serialize(ss);

    auto eh2 = probds::ExponentialHistogram::deserialize(ss);
    EXPECT_EQ(eh2.window_size(), 50u);
    EXPECT_EQ(eh2.current_time(), 30u);
    EXPECT_EQ(eh2.estimate_last_n(20), eh.estimate_last_n(20));
    EXPECT_EQ(eh2.estimate_last_n(50), eh.estimate_last_n(50));
}
