#pragma once
// =============================================================================
// bits.hpp — BitArray utilities for probDS
// =============================================================================

#include <cstdint>
#include <vector>
#include <algorithm>
#include <stdexcept>

#ifdef __AVX2__
#include <immintrin.h>
#endif

namespace probds {

class BitArray {
public:
    // =========================================================================
    // Construction
    // =========================================================================

    explicit BitArray(std::size_t num_bits) {
        if (num_bits == 0) {
            throw std::invalid_argument("BitArray: num_bits must be > 0");
        }
        num_bits_ = next_power_of_two(num_bits < 64 ? 64 : num_bits);
        mask_ = num_bits_ - 1;
        bits_.resize(num_bits_ / 64, 0);
    }

    // =========================================================================
    // Accessors and Mutators
    // =========================================================================

    /// Set bit at pos to 1
    void set(std::size_t pos) noexcept {
        const std::size_t masked_pos = pos & mask_;
        bits_[masked_pos >> 6] |= (std::uint64_t{1} << (masked_pos & 63));
    }

    /// Set bit at pos to 1 and return if it was previously 0
    bool set_and_test(std::size_t pos) noexcept {
        const std::size_t masked_pos = pos & mask_;
        const std::size_t word = masked_pos >> 6;
        const std::uint64_t bit = (std::uint64_t{1} << (masked_pos & 63));
        const bool was_zero = !(bits_[word] & bit);
        bits_[word] |= bit;
        return was_zero;
    }

    /// Check if bit at pos is 1
    [[nodiscard]] bool get(std::size_t pos) const noexcept {
        const std::size_t masked_pos = pos & mask_;
        return (bits_[masked_pos >> 6] & (std::uint64_t{1} << (masked_pos & 63))) != 0;
    }

    /// Total number of bits set to 1
    [[nodiscard]] std::size_t popcount() const noexcept {
        std::size_t count = 0;
        for (const auto word : bits_) {
            count += static_cast<std::size_t>(__builtin_popcountll(word));
        }
        return count;
    }

    /// Reset all bits to 0
    void clear() noexcept {
        std::fill(bits_.begin(), bits_.end(), 0);
    }

    /// Bitwise OR (union)
    void or_assign(const BitArray& other) {
        if (num_bits_ != other.num_bits_) {
            throw std::invalid_argument("BitArray: arrays must be of the same size");
        }
        for (std::size_t i = 0; i < bits_.size(); ++i) {
            bits_[i] |= other.bits_[i];
        }
    }

    /// Bitwise AND (intersection)
    void and_assign(const BitArray& other) {
        if (num_bits_ != other.num_bits_) {
            throw std::invalid_argument("BitArray: arrays must be of the same size");
        }
        for (std::size_t i = 0; i < bits_.size(); ++i) {
            bits_[i] &= other.bits_[i];
        }
    }

    // =========================================================================
    // SIMD Bulk Operations
    // =========================================================================

    /// AVX2-accelerated bulk query
    void get_bulk(const std::uint32_t* indices, bool* results, std::size_t count) const noexcept {
        std::size_t i = 0;
#ifdef __AVX2__
        const int* base_ptr = reinterpret_cast<const int*>(bits_.data());
        const int mask_val = static_cast<int>(mask_);
        const __m256i mask_vec = _mm256_set1_epi32(mask_val);
        const __m256i ones = _mm256_set1_epi32(1);
        const __m256i thirty_one = _mm256_set1_epi32(31);
        const __m256i zero = _mm256_setzero_si256();

        for (; i + 7 < count; i += 8) {
            // Load 8 indices
            __m256i idx = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(indices + i));
            // Apply bitmask for bounds
            idx = _mm256_and_si256(idx, mask_vec);

            // Compute 32-bit word index: idx >> 5
            __m256i word_idx = _mm256_srli_epi32(idx, 5);
            // Gather 8 32-bit words
            __m256i words = _mm256_i32gather_epi32(base_ptr, word_idx, 4);

            // Compute bit shift: idx & 31
            __m256i shifts = _mm256_and_si256(idx, thirty_one);
            // Compute bit mask: 1 << shifts
            __m256i bit_masks = _mm256_sllv_epi32(ones, shifts);

            // Check if bits are set: (words & bit_masks) != 0
            __m256i and_res = _mm256_and_si256(words, bit_masks);
            __m256i cmp = _mm256_cmpeq_epi32(and_res, zero);

            // cmp contains 0 for true (bit set), -1 for false (bit not set).
            // Extract to temporary array and store in results
            alignas(32) int temp[8];
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(temp), cmp);
            for (int j = 0; j < 8; ++j) {
                results[i + j] = (temp[j] == 0);
            }
        }
#endif
        // Scalar fallback
        for (; i < count; ++i) {
            results[i] = get(indices[i]);
        }
    }

    // =========================================================================
    // Introspection
    // =========================================================================

    [[nodiscard]] std::size_t size() const noexcept { return num_bits_; }
    [[nodiscard]] std::size_t memory_bytes() const noexcept { return bits_.size() * sizeof(std::uint64_t); }
    [[nodiscard]] const std::vector<std::uint64_t>& data() const noexcept { return bits_; }
    [[nodiscard]] std::vector<std::uint64_t>& data() noexcept { return bits_; }

private:
    static std::size_t next_power_of_two(std::size_t n) noexcept {
        if (n == 0) return 1;
        --n;
        n |= n >> 1;
        n |= n >> 2;
        n |= n >> 4;
        n |= n >> 8;
        n |= n >> 16;
        n |= n >> 32;
        return n + 1;
    }

    std::vector<std::uint64_t> bits_;
    std::size_t num_bits_;
    std::size_t mask_;
};

} // namespace probds
