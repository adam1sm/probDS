#pragma once
// =============================================================================
// blocked_bloom_filter.hpp — Blocked Bloom Filter
// probDS: High-Performance Probabilistic Data Structures Library
// =============================================================================

#include "hash.hpp"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <istream>
#include <ostream>
#include <stdexcept>
#include <array>
#include <utility>
#include <vector>

#ifdef __AVX2__
#include <immintrin.h>
#elif defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace probds {

template <typename T = std::string_view, typename Hash = DefaultHash<T>>
class BlockedBloomFilter {
public:
    static_assert(is_hash_policy_v<Hash, T>, "Hash must satisfy the HashPolicy concept");

    // Block structure aligned to 64-byte cache lines
    struct alignas(64) Block {
        std::uint64_t words[8]; // 8 * 8 bytes = 64 bytes = 512 bits
    };

    // =========================================================================
    // Construction
    // =========================================================================

    explicit BlockedBloomFilter(std::size_t expected_insertions,
                                double false_positive_rate = 0.01,
                                Hash hasher = Hash{})
        : count_(0), hasher_(std::move(hasher))
    {
        if (expected_insertions == 0) {
            throw std::invalid_argument("BlockedBloomFilter: expected_insertions must be > 0");
        }
        if (false_positive_rate <= 0.0 || false_positive_rate >= 1.0) {
            throw std::invalid_argument("BlockedBloomFilter: false_positive_rate must be in (0, 1)");
        }

        const double n = static_cast<double>(expected_insertions);
        const double ln2 = std::log(2.0);
        const double ln2_sq = ln2 * ln2;

        const auto m_opt = static_cast<std::size_t>(
            std::ceil(-n * std::log(false_positive_rate) / ln2_sq));

        // Total blocks = m / 512, rounded to next power of 2
        const std::size_t base_blocks = (m_opt + 511) / 512;
        num_blocks_ = next_power_of_two(base_blocks < 1 ? 1 : base_blocks);
        block_mask_ = num_blocks_ - 1;

        // Recompute hashes for optimal FPR inside a 512-bit block
        const double bits_per_item = 512.0 / (n / static_cast<double>(num_blocks_));
        num_hashes_ = static_cast<std::size_t>(std::round(bits_per_item * ln2));
        if (num_hashes_ == 0) num_hashes_ = 1;
        if (num_hashes_ > 30) num_hashes_ = 30; // Cap to keep hashing latency low

        blocks_.resize(num_blocks_);
        clear();
    }

    // =========================================================================
    // Core Operations
    // =========================================================================

    /// Insert a single key
    void insert(const T& key) {
        const auto [h1, h2] = get_hash_pair(key);
        const std::size_t block_idx = h1 & block_mask_;
        auto& block = blocks_[block_idx];

        for (std::size_t i = 0; i < num_hashes_; ++i) {
            const std::size_t pos = (h2 + i * h1) & 511;
            block.words[pos >> 6] |= (std::uint64_t{1} << (pos & 63));
        }
        ++count_;
    }

    /// Bulk insert
    template <typename InputIt>
    void insert(InputIt first, InputIt last) {
        for (; first != last; ++first) {
            insert(*first);
        }
    }

    /// Query set membership
    [[nodiscard]] bool possibly_contains(const T& key) const noexcept {
        const auto [h1, h2] = get_hash_pair(key);
        const std::size_t block_idx = h1 & block_mask_;
        const auto& block = blocks_[block_idx];

        for (std::size_t i = 0; i < num_hashes_; ++i) {
            const std::size_t pos = (h2 + i * h1) & 511;
            if (!(block.words[pos >> 6] & (std::uint64_t{1} << (pos & 63)))) {
                return false;
            }
        }
        return true;
    }

    /// SIMD accelerated bulk lookup
    void possibly_contains_bulk(const T* keys, bool* results, std::size_t count) const noexcept {
        std::size_t i = 0;
#if defined(__AVX2__)
        alignas(32) std::uint64_t h1_arr[8];
        alignas(32) std::uint64_t h2_arr[8];
        alignas(32) std::uint32_t gather_indices[8];
        alignas(32) std::uint32_t shifts[8];

        const int* base_ptr = reinterpret_cast<const int*>(blocks_.data());
        const __m256i ones = _mm256_set1_epi32(1);
        const __m256i zero = _mm256_setzero_si256();

        for (; i + 7 < count; i += 8) {
            for (int j = 0; j < 8; ++j) {
                auto [h1, h2] = get_hash_pair(keys[i + j]);
                h1_arr[j] = h1;
                h2_arr[j] = h2;
                results[i + j] = true;
            }

            for (std::size_t h_idx = 0; h_idx < num_hashes_; ++h_idx) {
                for (int j = 0; j < 8; ++j) {
                    const std::size_t b_idx = h1_arr[j] & block_mask_;
                    const std::size_t pos = (h2_arr[j] + h_idx * h1_arr[j]) & 511;
                    gather_indices[j] = static_cast<std::uint32_t>(b_idx * 16 + (pos >> 5));
                    shifts[j] = static_cast<std::uint32_t>(pos & 31);
                }

                __m256i idx = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(gather_indices));
                __m256i words = _mm256_i32gather_epi32(base_ptr, idx, 4);

                __m256i sh = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(shifts));
                __m256i bit_masks = _mm256_sllv_epi32(ones, sh);

                __m256i and_res = _mm256_and_si256(words, bit_masks);
                __m256i cmp = _mm256_cmpeq_epi32(and_res, zero);

                alignas(32) int temp[8];
                _mm256_storeu_si256(reinterpret_cast<__m256i*>(temp), cmp);

                for (int j = 0; j < 8; ++j) {
                    results[i + j] = results[i + j] && (temp[j] == 0);
                }
            }
        }
#elif defined(__aarch64__)
        alignas(16) std::uint64_t h1_arr[4];
        alignas(16) std::uint64_t h2_arr[4];
        alignas(16) std::uint32_t shifts[4];
        alignas(16) std::uint32_t loaded_words[4];

        const uint32x4_t v_ones = vdupq_n_u32(1);
        const uint32x4_t v_zero = vdupq_n_u32(0);

        for (; i + 3 < count; i += 4) {
            for (int j = 0; j < 4; ++j) {
                auto [h1, h2] = get_hash_pair(keys[i + j]);
                h1_arr[j] = h1;
                h2_arr[j] = h2;
                results[i + j] = true;
            }

            for (std::size_t h_idx = 0; h_idx < num_hashes_; ++h_idx) {
                for (int j = 0; j < 4; ++j) {
                    const std::size_t b_idx = h1_arr[j] & block_mask_;
                    const std::size_t pos = (h2_arr[j] + h_idx * h1_arr[j]) & 511;
                    const auto& block = blocks_[b_idx];
                    const std::uint32_t* block_words_32 = reinterpret_cast<const std::uint32_t*>(block.words);
                    loaded_words[j] = block_words_32[pos >> 5];
                    shifts[j] = static_cast<std::uint32_t>(pos & 31);
                }

                uint32x4_t v_words = vld1q_u32(loaded_words);
                uint32x4_t v_shifts = vld1q_u32(shifts);
                int32x4_t v_shifts_signed = vreinterpretq_s32_u32(v_shifts);
                uint32x4_t v_bit_masks = vshlq_u32(v_ones, v_shifts_signed);

                uint32x4_t v_and_res = vandq_u32(v_words, v_bit_masks);
                uint32x4_t v_cmp = vceqq_u32(v_and_res, v_zero);

                alignas(16) std::uint32_t temp[4];
                vst1q_u32(temp, v_cmp);

                for (int j = 0; j < 4; ++j) {
                    results[i + j] = results[i + j] && (temp[j] == 0);
                }
            }
        }
#endif
        // Scalar fallback
        for (; i < count; ++i) {
            results[i] = possibly_contains(keys[i]);
        }
    }

    /// Batched lookup with prefetching and vectorization
    template <std::size_t N>
    [[nodiscard]] std::array<bool, N> possibly_contains_batch(
        const std::array<const T*, N>& keys) const noexcept
    {
        alignas(16) std::uint64_t h1_arr[N];
        alignas(16) std::uint64_t h2_arr[N];

        // 1. Compute all N hash pairs and prefetch all blocks first.
        for (std::size_t i = 0; i < N; ++i) {
            auto [h1, h2] = get_hash_pair(*keys[i]);
            h1_arr[i] = h1;
            h2_arr[i] = h2;
            const std::size_t block_idx = h1 & block_mask_;
            __builtin_prefetch(&blocks_[block_idx], 0, 3);
        }

        std::array<bool, N> results;
        results.fill(true);

        std::size_t i = 0;

        // 2. On ARM64, process in chunks of 4 using NEON.
#if defined(__aarch64__)
        for (; i + 3 < N; i += 4) {
            const std::uint32_t* block_words_32[4];
            for (int j = 0; j < 4; ++j) {
                const std::size_t b_idx = h1_arr[i + j] & block_mask_;
                block_words_32[j] = reinterpret_cast<const std::uint32_t*>(blocks_[b_idx].words);
            }

            uint32x4_t v_h1 = {
                static_cast<uint32_t>(h1_arr[i]),
                static_cast<uint32_t>(h1_arr[i + 1]),
                static_cast<uint32_t>(h1_arr[i + 2]),
                static_cast<uint32_t>(h1_arr[i + 3])
            };
            uint32x4_t v_h2 = {
                static_cast<uint32_t>(h2_arr[i]),
                static_cast<uint32_t>(h2_arr[i + 1]),
                static_cast<uint32_t>(h2_arr[i + 2]),
                static_cast<uint32_t>(h2_arr[i + 3])
            };

            const uint32x4_t v_ones = vdupq_n_u32(1);
            const uint32x4_t v_zero = vdupq_n_u32(0);
            const uint32x4_t v_mask = vdupq_n_u32(511);
            const uint32x4_t v_shift_mask = vdupq_n_u32(31);

            for (std::size_t h_idx = 0; h_idx < num_hashes_; ++h_idx) {
                // Early exit if all 4 results in this chunk are already false.
                if (!results[i] && !results[i + 1] && !results[i + 2] && !results[i + 3]) {
                    break;
                }

                uint32x4_t v_h_idx = vdupq_n_u32(static_cast<uint32_t>(h_idx));
                uint32x4_t v_pos = vandq_u32(vaddq_u32(v_h2, vmulq_u32(v_h_idx, v_h1)), v_mask);
                uint32x4_t v_pos_shifted = vshrq_n_u32(v_pos, 5);
                uint32x4_t v_shifts = vandq_u32(v_pos, v_shift_mask);

                uint32x4_t v_words = vdupq_n_u32(0);
                v_words = vsetq_lane_u32(block_words_32[0][vgetq_lane_u32(v_pos_shifted, 0)], v_words, 0);
                v_words = vsetq_lane_u32(block_words_32[1][vgetq_lane_u32(v_pos_shifted, 1)], v_words, 1);
                v_words = vsetq_lane_u32(block_words_32[2][vgetq_lane_u32(v_pos_shifted, 2)], v_words, 2);
                v_words = vsetq_lane_u32(block_words_32[3][vgetq_lane_u32(v_pos_shifted, 3)], v_words, 3);

                int32x4_t v_shifts_signed = vreinterpretq_s32_u32(v_shifts);
                uint32x4_t v_bit_masks = vshlq_u32(v_ones, v_shifts_signed);

                uint32x4_t v_and_res = vandq_u32(v_words, v_bit_masks);
                uint32x4_t v_cmp = vceqq_u32(v_and_res, v_zero);

                results[i]     = results[i]     && (vgetq_lane_u32(v_cmp, 0) == 0);
                results[i + 1] = results[i + 1] && (vgetq_lane_u32(v_cmp, 1) == 0);
                results[i + 2] = results[i + 2] && (vgetq_lane_u32(v_cmp, 2) == 0);
                results[i + 3] = results[i + 3] && (vgetq_lane_u32(v_cmp, 3) == 0);
            }
        }
#endif

        // 3. Scalar fallback for remainder elements.
        for (; i < N; ++i) {
            const std::size_t b_idx = h1_arr[i] & block_mask_;
            const auto& block = blocks_[b_idx];
            for (std::size_t h_idx = 0; h_idx < num_hashes_; ++h_idx) {
                const std::size_t pos = (h2_arr[i] + h_idx * h1_arr[i]) & 511;
                if (!(block.words[pos >> 6] & (std::uint64_t{1} << (pos & 63)))) {
                    results[i] = false;
                    break;
                }
            }
        }

        return results;
    }

    // =========================================================================
    // Set Operations
    // =========================================================================

    BlockedBloomFilter& operator|=(const BlockedBloomFilter& other) {
        check_compatible(other, "operator|=");
        for (std::size_t i = 0; i < blocks_.size(); ++i) {
            for (int j = 0; j < 8; ++j) {
                blocks_[i].words[j] |= other.blocks_[i].words[j];
            }
        }
        count_ += other.count_;
        return *this;
    }

    BlockedBloomFilter& operator&=(const BlockedBloomFilter& other) {
        check_compatible(other, "operator&=");
        for (std::size_t i = 0; i < blocks_.size(); ++i) {
            for (int j = 0; j < 8; ++j) {
                blocks_[i].words[j] &= other.blocks_[i].words[j];
            }
        }
        count_ = std::min(count_, other.count_);
        return *this;
    }

    friend BlockedBloomFilter operator|(BlockedBloomFilter lhs, const BlockedBloomFilter& rhs) {
        lhs |= rhs;
        return lhs;
    }

    friend BlockedBloomFilter operator&(BlockedBloomFilter lhs, const BlockedBloomFilter& rhs) {
        lhs &= rhs;
        return lhs;
    }

    // =========================================================================
    // Statistics & Introspection
    // =========================================================================

    [[nodiscard]] std::size_t bit_count() const noexcept { return num_blocks_ * 512; }
    [[nodiscard]] std::size_t block_count() const noexcept { return num_blocks_; }
    [[nodiscard]] std::size_t hash_count() const noexcept { return num_hashes_; }
    [[nodiscard]] std::size_t size() const noexcept { return count_; }
    [[nodiscard]] std::size_t memory_bytes() const noexcept { return num_blocks_ * sizeof(Block); }
    [[nodiscard]] std::size_t memory_usage() const noexcept { return memory_bytes(); }

    [[nodiscard]] double fill_ratio() const noexcept {
        std::size_t total_set = 0;
        for (const auto& block : blocks_) {
            for (int j = 0; j < 8; ++j) {
                total_set += static_cast<std::size_t>(__builtin_popcountll(block.words[j]));
            }
        }
        return static_cast<double>(total_set) / static_cast<double>(bit_count());
    }

    [[nodiscard]] double expected_fpr() const noexcept {
        if (count_ == 0) return 0.0;
        const double n_per_block = static_cast<double>(count_) / static_cast<double>(num_blocks_);
        const double exponent = -static_cast<double>(num_hashes_) * n_per_block / 512.0;
        return std::pow(1.0 - std::exp(exponent), static_cast<double>(num_hashes_));
    }

    void clear() noexcept {
        for (auto& block : blocks_) {
            std::fill(std::begin(block.words), std::end(block.words), 0ULL);
        }
        count_ = 0;
    }

    // =========================================================================
    // Serialization
    // =========================================================================

    void serialize(std::ostream& out) const {
        out.write("PBB2", 4);
        write_u64(out, num_blocks_);
        write_u64(out, num_hashes_);
        write_u64(out, count_);
        out.write(reinterpret_cast<const char*>(blocks_.data()), static_cast<std::streamsize>(num_blocks_ * sizeof(Block)));
        if (!out) {
            throw std::runtime_error("BlockedBloomFilter::serialize: write failed");
        }
    }

    static BlockedBloomFilter deserialize(std::istream& in, Hash hasher = Hash{}) {
        char magic[4];
        in.read(magic, 4);
        if (std::memcmp(magic, "PBB2", 4) != 0) {
            throw std::runtime_error("BlockedBloomFilter::deserialize: invalid magic number");
        }

        const auto num_blocks = read_u64(in);
        const auto num_hashes = read_u64(in);
        const auto count = read_u64(in);

        BlockedBloomFilter filter(num_blocks, num_hashes, count, std::move(hasher));
        in.read(reinterpret_cast<char*>(filter.blocks_.data()), static_cast<std::streamsize>(num_blocks * sizeof(Block)));

        if (!in) {
            throw std::runtime_error("BlockedBloomFilter::deserialize: read failed");
        }
        return filter;
    }

private:
    BlockedBloomFilter(std::size_t num_blocks, std::size_t num_hashes, std::size_t count, Hash hasher)
        : count_(count), num_blocks_(num_blocks), block_mask_(num_blocks - 1),
          num_hashes_(num_hashes), hasher_(std::move(hasher))
    {
        blocks_.resize(num_blocks_);
    }

    static constexpr std::uint64_t fmix64(std::uint64_t k) noexcept {
        k ^= k >> 33;
        k *= 0xff51afd7ed558ccdULL;
        k ^= k >> 33;
        k *= 0xc4ceb9fe1a85ec53ULL;
        k ^= k >> 33;
        return k;
    }

    [[nodiscard]] std::pair<std::uint64_t, std::uint64_t> get_hash_pair(const T& key) const noexcept {
        const std::uint64_t hash_val = hasher_(key);
        return {fmix64(hash_val), fmix64(hash_val ^ 0x6c62272e07bb0142ULL)};
    }

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

    void check_compatible(const BlockedBloomFilter& other, const char* op) const {
        if (num_blocks_ != other.num_blocks_ || num_hashes_ != other.num_hashes_) {
            throw std::invalid_argument(
                std::string("BlockedBloomFilter::") + op + ": filters are incompatible");
        }
    }

    static void write_u64(std::ostream& out, std::uint64_t v) {
        out.write(reinterpret_cast<const char*>(&v), sizeof(v));
    }
    static std::uint64_t read_u64(std::istream& in) {
        std::uint64_t v;
        in.read(reinterpret_cast<char*>(&v), sizeof(v));
        return v;
    }

    std::size_t count_;
    std::size_t num_blocks_;
    std::size_t block_mask_;
    std::size_t num_hashes_;
    Hash hasher_;
    std::vector<Block> blocks_;
};

} // namespace probds
