#pragma once
// =============================================================================
// counting_bloom_filter.hpp — Counting Bloom Filter
// probDS: High-Performance Probabilistic Data Structures Library
// =============================================================================

#include "hash.hpp"
#include <algorithm>
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
class CountingBloomFilter {
public:
    static_assert(is_hash_policy_v<Hash, T>, "Hash must satisfy the HashPolicy concept");

    // =========================================================================
    // Construction
    // =========================================================================

    explicit CountingBloomFilter(std::size_t expected_insertions,
                                 double false_positive_rate = 0.01,
                                 Hash hasher = Hash{})
        : count_(0), hasher_(std::move(hasher))
    {
        if (expected_insertions == 0) {
            throw std::invalid_argument("CountingBloomFilter: expected_insertions must be > 0");
        }
        if (false_positive_rate <= 0.0 || false_positive_rate >= 1.0) {
            throw std::invalid_argument("CountingBloomFilter: false_positive_rate must be in (0, 1)");
        }

        const double n = static_cast<double>(expected_insertions);
        const double ln2 = std::log(2.0);
        const double ln2_sq = ln2 * ln2;

        const auto m_opt = static_cast<std::size_t>(
            std::ceil(-n * std::log(false_positive_rate) / ln2_sq));

        num_counters_ = next_power_of_two(m_opt < 64 ? 64 : m_opt);
        mask_ = num_counters_ - 1;

        num_hashes_ = static_cast<std::size_t>(
            std::round(static_cast<double>(num_counters_) / n * ln2));
        if (num_hashes_ == 0) num_hashes_ = 1;

        counters_.assign(num_counters_, 0);
    }

    // =========================================================================
    // Core Operations
    // =========================================================================

    /// Insert a single key
    void insert(const T& key) {
        const auto [h1, h2] = get_hash_pair(key);
        for (std::size_t i = 0; i < num_hashes_; ++i) {
            const std::size_t pos = nth_hash(i, h1, h2);
            // Saturated increment (prevent overflow at 255)
            if (counters_[pos] < 255) {
                ++counters_[pos];
            }
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
        for (std::size_t i = 0; i < num_hashes_; ++i) {
            if (counters_[nth_hash(i, h1, h2)] == 0) {
                return false;
            }
        }
        return true;
    }

    /// Remove a key
    bool remove(const T& key) {
        if (!possibly_contains(key)) {
            return false;
        }

        const auto [h1, h2] = get_hash_pair(key);
        for (std::size_t i = 0; i < num_hashes_; ++i) {
            const std::size_t pos = nth_hash(i, h1, h2);
            // Do not decrement if counter is saturated (255) to prevent corruption
            if (counters_[pos] > 0 && counters_[pos] < 255) {
                --counters_[pos];
            }
        }
        if (count_ > 0) {
            --count_;
        }
        return true;
    }

    // =========================================================================
    // Set Operations
    // =========================================================================

    CountingBloomFilter& operator|=(const CountingBloomFilter& other) {
        check_compatible(other, "operator|=");
        for (std::size_t i = 0; i < num_counters_; ++i) {
            counters_[i] = std::max(counters_[i], other.counters_[i]);
        }
        count_ += other.count_;
        return *this;
    }

    CountingBloomFilter& operator&=(const CountingBloomFilter& other) {
        check_compatible(other, "operator&=");
        for (std::size_t i = 0; i < num_counters_; ++i) {
            counters_[i] = std::min(counters_[i], other.counters_[i]);
        }
        count_ = std::min(count_, other.count_);
        return *this;
    }

    friend CountingBloomFilter operator|(CountingBloomFilter lhs, const CountingBloomFilter& rhs) {
        lhs |= rhs;
        return lhs;
    }

    friend CountingBloomFilter operator&(CountingBloomFilter lhs, const CountingBloomFilter& rhs) {
        lhs &= rhs;
        return lhs;
    }

    // =========================================================================
    // Statistics & Introspection
    // =========================================================================

    [[nodiscard]] std::size_t bit_count() const noexcept { return num_counters_; }
    [[nodiscard]] std::size_t hash_count() const noexcept { return num_hashes_; }
    [[nodiscard]] std::size_t size() const noexcept { return count_; }
    [[nodiscard]] std::size_t memory_bytes() const noexcept { return counters_.size() * sizeof(std::uint8_t); }
    [[nodiscard]] std::size_t memory_usage() const noexcept { return memory_bytes(); }

    [[nodiscard]] double fill_ratio() const noexcept {
        std::size_t non_zero = 0;
        for (const auto val : counters_) {
            if (val > 0) ++non_zero;
        }
        return static_cast<double>(non_zero) / static_cast<double>(num_counters_);
    }

    [[nodiscard]] double expected_fpr() const noexcept {
        if (count_ == 0) return 0.0;
        const double exponent = -static_cast<double>(num_hashes_) *
                                static_cast<double>(count_) /
                                static_cast<double>(num_counters_);
        return std::pow(1.0 - std::exp(exponent), static_cast<double>(num_hashes_));
    }

    void clear() noexcept {
        std::fill(counters_.begin(), counters_.end(), std::uint8_t{0});
        count_ = 0;
    }

    // =========================================================================
    // Serialization
    // =========================================================================

    void serialize(std::ostream& out) const {
        out.write("PCB2", 4);
        write_u64(out, num_counters_);
        write_u64(out, num_hashes_);
        write_u64(out, count_);
        out.write(reinterpret_cast<const char*>(counters_.data()), static_cast<std::streamsize>(num_counters_));
        if (!out) {
            throw std::runtime_error("CountingBloomFilter::serialize: write failed");
        }
    }

    static CountingBloomFilter deserialize(std::istream& in, Hash hasher = Hash{}) {
        char magic[4];
        in.read(magic, 4);
        if (std::memcmp(magic, "PCB2", 4) != 0) {
            throw std::runtime_error("CountingBloomFilter::deserialize: invalid magic number");
        }

        const auto num_counters = read_u64(in);
        const auto num_hashes = read_u64(in);
        const auto count = read_u64(in);

        CountingBloomFilter filter(num_counters, num_hashes, count, std::move(hasher));
        in.read(reinterpret_cast<char*>(filter.counters_.data()), static_cast<std::streamsize>(num_counters));

        if (!in) {
            throw std::runtime_error("CountingBloomFilter::deserialize: read failed");
        }
        return filter;
    }

private:
    CountingBloomFilter(std::size_t num_counters, std::size_t num_hashes, std::size_t count, Hash hasher)
        : count_(count), num_counters_(num_counters), mask_(num_counters - 1),
          num_hashes_(num_hashes), hasher_(std::move(hasher))
    {
        counters_.assign(num_counters_, 0);
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

    [[nodiscard]] std::size_t nth_hash(std::size_t i, std::uint64_t h1, std::uint64_t h2) const noexcept {
        return static_cast<std::size_t>((h1 + i * h2) & mask_);
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

    void check_compatible(const CountingBloomFilter& other, const char* op) const {
        if (num_counters_ != other.num_counters_ || num_hashes_ != other.num_hashes_) {
            throw std::invalid_argument(
                std::string("CountingBloomFilter::") + op + ": filters are incompatible");
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
    std::size_t num_counters_;
    std::size_t mask_;
    std::size_t num_hashes_;
    Hash hasher_;
    std::vector<std::uint8_t> counters_;
};

} // namespace probds
