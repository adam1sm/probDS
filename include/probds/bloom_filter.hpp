#pragma once
// =============================================================================
// bloom_filter.hpp — Bloom Filter
// probDS: High-Performance Probabilistic Data Structures Library
// =============================================================================

#include "hash.hpp"
#include "bits.hpp"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <istream>
#include <ostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace probds {

template <typename T = std::string_view, typename Hash = DefaultHash<T>>
class BloomFilter {
public:
    static_assert(is_hash_policy_v<Hash, T>, "Hash must satisfy the HashPolicy concept");

    // =========================================================================
    // Construction
    // =========================================================================

    explicit BloomFilter(std::size_t expected_insertions,
                         double false_positive_rate = 0.01,
                         Hash hasher = Hash{})
        : count_(0), hasher_(std::move(hasher)), bits_(1) // Initial temporary size
    {
        if (expected_insertions == 0) {
            throw std::invalid_argument("BloomFilter: expected_insertions must be > 0");
        }
        if (false_positive_rate <= 0.0 || false_positive_rate >= 1.0) {
            throw std::invalid_argument("BloomFilter: false_positive_rate must be in (0, 1)");
        }

        const double n = static_cast<double>(expected_insertions);
        const double ln2 = std::log(2.0);
        const double ln2_sq = ln2 * ln2;

        // Optimal m (before power-of-2 rounding)
        const auto m_opt = static_cast<std::size_t>(
            std::ceil(-n * std::log(false_positive_rate) / ln2_sq));

        // Create BitArray (which handles rounding up to power-of-2 automatically)
        bits_ = BitArray(m_opt < 64 ? 64 : m_opt);

        // Recompute k for the actual rounded bit count
        num_hashes_ = static_cast<std::size_t>(
            std::round(static_cast<double>(bits_.size()) / n * ln2));
        if (num_hashes_ == 0) num_hashes_ = 1;
    }

    // =========================================================================
    // Core Operations
    // =========================================================================

    /// Insert a single key
    void insert(const T& key) {
        const auto [h1, h2] = get_hash_pair(key);
        for (std::size_t i = 0; i < num_hashes_; ++i) {
            bits_.set(nth_hash(i, h1, h2));
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
        return possibly_contains(h1, h2);
    }

    [[nodiscard]] std::pair<std::uint64_t, std::uint64_t> get_hash_pair(const T& key) const noexcept {
        const std::uint64_t hash_val = hasher_(key);
        return {fmix64(hash_val), fmix64(hash_val ^ 0x6c62272e07bb0142ULL)};
    }

    [[nodiscard]] bool possibly_contains(std::uint64_t h1, std::uint64_t h2) const noexcept {
        const std::uint64_t* data_ptr = bits_.data().data();
        const std::size_t mask = bits_.size() - 1;
        const std::size_t num_hashes = num_hashes_;
        for (std::size_t i = 0; i < num_hashes; ++i) {
            const std::size_t pos = (h1 + i * h2) & mask;
            if (!(data_ptr[pos >> 6] & (std::uint64_t{1} << (pos & 63)))) {
                return false;
            }
        }
        return true;
    }

    /// AVX2 accelerated bulk lookup
    void possibly_contains_bulk(const T* keys, bool* results, std::size_t count) const noexcept {
        std::size_t i = 0;
#ifdef __AVX2__
        alignas(32) std::uint64_t h1_arr[8];
        alignas(32) std::uint64_t h2_arr[8];
        alignas(32) std::uint32_t indices[8];
        alignas(32) bool lane_results[8];

        for (; i + 7 < count; i += 8) {
            // Precompute hash pairs for the 8 keys
            for (int j = 0; j < 8; ++j) {
                auto [h1, h2] = get_hash_pair(keys[i + j]);
                h1_arr[j] = h1;
                h2_arr[j] = h2;
                results[i + j] = true; // Initialize to true
            }

            // For each of the k hash functions, check all 8 keys
            for (std::size_t h_idx = 0; h_idx < num_hashes_; ++h_idx) {
                for (int j = 0; j < 8; ++j) {
                    indices[j] = static_cast<std::uint32_t>(nth_hash(h_idx, h1_arr[j], h2_arr[j]));
                }
                bits_.get_bulk(indices, lane_results, 8);
                for (int j = 0; j < 8; ++j) {
                    results[i + j] = results[i + j] && lane_results[j];
                }
            }
        }
#endif
        // Scalar fallback
        for (; i < count; ++i) {
            results[i] = possibly_contains(keys[i]);
        }
    }

    // =========================================================================
    // Set Operations
    // =========================================================================

    BloomFilter& operator|=(const BloomFilter& other) {
        check_compatible(other, "operator|=");
        bits_.or_assign(other.bits_);
        count_ += other.count_;
        return *this;
    }

    BloomFilter& operator&=(const BloomFilter& other) {
        check_compatible(other, "operator&=");
        bits_.and_assign(other.bits_);
        count_ = std::min(count_, other.count_);
        return *this;
    }

    friend BloomFilter operator|(BloomFilter lhs, const BloomFilter& rhs) {
        lhs |= rhs;
        return lhs;
    }

    friend BloomFilter operator&(BloomFilter lhs, const BloomFilter& rhs) {
        lhs &= rhs;
        return lhs;
    }

    // =========================================================================
    // Serialization
    // =========================================================================

    void serialize(std::ostream& out) const {
        out.write("PBF2", 4); // New magic for v2 (templated/BitArray)
        write_u64(out, bits_.size());
        write_u64(out, num_hashes_);
        write_u64(out, count_);
        out.write(reinterpret_cast<const char*>(bits_.data().data()),
                  static_cast<std::streamsize>(bits_.data().size() * sizeof(std::uint64_t)));
        if (!out) {
            throw std::runtime_error("BloomFilter::serialize: write failed");
        }
    }

    static BloomFilter deserialize(std::istream& in, Hash hasher = Hash{}) {
        char magic[4];
        in.read(magic, 4);
        if (std::memcmp(magic, "PBF2", 4) != 0) {
            throw std::runtime_error("BloomFilter::deserialize: invalid magic number");
        }

        const auto num_bits = read_u64(in);
        const auto num_hashes = read_u64(in);
        const auto count = read_u64(in);

        const auto num_words = num_bits / 64;
        std::vector<std::uint64_t> words(num_words);
        in.read(reinterpret_cast<char*>(words.data()),
                static_cast<std::streamsize>(num_words * sizeof(std::uint64_t)));

        if (!in) {
            throw std::runtime_error("BloomFilter::deserialize: read failed");
        }

        BloomFilter filter(num_bits, num_hashes, count, std::move(words), std::move(hasher));
        return filter;
    }

    // =========================================================================
    // Statistics & Introspection
    // =========================================================================

    [[nodiscard]] std::size_t bit_count() const noexcept { return bits_.size(); }
    [[nodiscard]] std::size_t hash_count() const noexcept { return num_hashes_; }
    [[nodiscard]] std::size_t size() const noexcept { return count_; }
    [[nodiscard]] std::size_t memory_bytes() const noexcept { return bits_.memory_bytes(); }
    [[nodiscard]] std::size_t memory_usage() const noexcept { return memory_bytes(); }

    [[nodiscard]] double fill_ratio() const noexcept {
        return static_cast<double>(bits_.popcount()) / static_cast<double>(bits_.size());
    }

    [[nodiscard]] double expected_fpr() const noexcept {
        if (count_ == 0) return 0.0;
        const double exponent = -static_cast<double>(num_hashes_) *
                                static_cast<double>(count_) /
                                static_cast<double>(bits_.size());
        return std::pow(1.0 - std::exp(exponent), static_cast<double>(num_hashes_));
    }

    void clear() noexcept {
        bits_.clear();
        count_ = 0;
    }

private:
    // Constructor for deserialization
    BloomFilter(std::size_t num_bits, std::size_t num_hashes, std::size_t count,
                std::vector<std::uint64_t> words, Hash hasher)
        : count_(count), num_hashes_(num_hashes), hasher_(std::move(hasher)), bits_(num_bits)
    {
        bits_.data() = std::move(words);
    }

    // Helpers
    static constexpr std::uint64_t fmix64(std::uint64_t k) noexcept {
        k ^= k >> 33;
        k *= 0xff51afd7ed558ccdULL;
        k ^= k >> 33;
        k *= 0xc4ceb9fe1a85ec53ULL;
        k ^= k >> 33;
        return k;
    }

    // Helpers

    [[nodiscard]] std::size_t nth_hash(std::size_t i, std::uint64_t h1, std::uint64_t h2) const noexcept {
        return static_cast<std::size_t>(h1 + i * h2);
    }

    void check_compatible(const BloomFilter& other, const char* op) const {
        if (bits_.size() != other.bits_.size() || num_hashes_ != other.num_hashes_) {
            throw std::invalid_argument(
                std::string("BloomFilter::") + op + ": filters are incompatible");
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
    std::size_t num_hashes_;
    Hash hasher_;
    BitArray bits_;
};

} // namespace probds
