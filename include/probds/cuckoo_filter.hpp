#pragma once
// =============================================================================
// cuckoo_filter.hpp — Cuckoo Filter
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
class CuckooFilter {
public:
    static_assert(is_hash_policy_v<Hash, T>, "Hash must satisfy the HashPolicy concept");

    // =========================================================================
    // Construction
    // =========================================================================

    explicit CuckooFilter(std::size_t capacity,
                          std::size_t bucket_size = 4,
                          std::size_t fingerprint_bits = 8,
                          Hash hasher = Hash{})
        : bucket_size_(bucket_size),
          fingerprint_bits_(fingerprint_bits),
          count_(0),
          rng_state_(0xdeadbeefcafe1234ULL),
          hasher_(std::move(hasher))
    {
        if (capacity == 0) {
            throw std::invalid_argument("CuckooFilter: capacity must be > 0");
        }
        if (bucket_size == 0) {
            throw std::invalid_argument("CuckooFilter: bucket_size must be > 0");
        }
        if (fingerprint_bits == 0 || fingerprint_bits > 8) {
            throw std::invalid_argument("CuckooFilter: fingerprint_bits must be in [1, 8]");
        }

        // num_buckets = next_power_of_2(ceil(capacity / bucket_size))
        const std::size_t min_buckets = (capacity + bucket_size - 1) / bucket_size;
        num_buckets_ = next_power_of_two(min_buckets < 2 ? 2 : min_buckets);
        bucket_mask_ = num_buckets_ - 1;
        fingerprint_mask_ = static_cast<std::uint8_t>((1u << fingerprint_bits) - 1);

        buckets_.assign(num_buckets_, std::vector<std::uint8_t>(bucket_size_, 0));
    }

    // =========================================================================
    // Core Operations
    // =========================================================================

    /// Insert a single key
    bool insert(const T& key) {
        const std::uint64_t h = full_hash(key);
        std::uint8_t fp = fingerprint(h);
        std::size_t i1 = index_hash(h);
        std::size_t i2 = alt_index(i1, fp);

        if (insert_to_bucket(i1, fp)) {
            ++count_;
            return true;
        }
        if (insert_to_bucket(i2, fp)) {
            ++count_;
            return true;
        }

        // Displacement
        std::size_t i = (xorshift64() & 1u) ? i1 : i2;
        for (std::size_t n = 0; n < kMaxKicks; ++n) {
            const std::size_t slot = xorshift64() % bucket_size_;
            std::swap(fp, buckets_[i][slot]);
            i = alt_index(i, fp);

            if (insert_to_bucket(i, fp)) {
                ++count_;
                return true;
            }
        }

        return false;
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
        const std::uint64_t h = full_hash(key);
        const std::uint8_t fp = fingerprint(h);
        const std::size_t i1 = index_hash(h);
        const std::size_t i2 = alt_index(i1, fp);

        return bucket_contains(i1, fp) || bucket_contains(i2, fp);
    }

    /// Remove a key
    bool remove(const T& key) {
        const std::uint64_t h = full_hash(key);
        const std::uint8_t fp = fingerprint(h);
        const std::size_t i1 = index_hash(h);
        const std::size_t i2 = alt_index(i1, fp);

        if (remove_from_bucket(i1, fp)) {
            --count_;
            return true;
        }
        if (remove_from_bucket(i2, fp)) {
            --count_;
            return true;
        }

        return false;
    }

    // =========================================================================
    // Statistics & Introspection
    // =========================================================================

    [[nodiscard]] std::size_t size() const noexcept { return count_; }
    [[nodiscard]] std::size_t capacity() const noexcept { return num_buckets_ * bucket_size_; }
    [[nodiscard]] double load_factor() const noexcept { return static_cast<double>(count_) / static_cast<double>(capacity()); }
    [[nodiscard]] double expected_fpr() const noexcept { return 2.0 * static_cast<double>(bucket_size_) / static_cast<double>(1u << fingerprint_bits_); }
    [[nodiscard]] std::size_t memory_bytes() const noexcept { return num_buckets_ * bucket_size_ * sizeof(std::uint8_t); }
    [[nodiscard]] std::size_t memory_usage() const noexcept { return memory_bytes(); }

    void clear() noexcept {
        for (auto& bucket : buckets_) {
            std::fill(bucket.begin(), bucket.end(), static_cast<std::uint8_t>(0));
        }
        count_ = 0;
    }

    // =========================================================================
    // Serialization
    // =========================================================================

    void serialize(std::ostream& out) const {
        out.write("PCF2", 4);
        write_u64(out, bucket_size_);
        write_u64(out, fingerprint_bits_);
        write_u64(out, num_buckets_);
        write_u64(out, count_);
        for (const auto& bucket : buckets_) {
            out.write(reinterpret_cast<const char*>(bucket.data()), static_cast<std::streamsize>(bucket_size_));
        }
        if (!out) {
            throw std::runtime_error("CuckooFilter::serialize: write failed");
        }
    }

    static CuckooFilter deserialize(std::istream& in, Hash hasher = Hash{}) {
        char magic[4];
        in.read(magic, 4);
        if (std::memcmp(magic, "PCF2", 4) != 0) {
            throw std::runtime_error("CuckooFilter::deserialize: invalid magic number");
        }

        const auto bucket_size = read_u64(in);
        const auto fingerprint_bits = read_u64(in);
        const auto num_buckets = read_u64(in);
        const auto count = read_u64(in);

        CuckooFilter filter(num_buckets, bucket_size, fingerprint_bits, count, std::move(hasher));
        for (auto& bucket : filter.buckets_) {
            in.read(reinterpret_cast<char*>(bucket.data()), static_cast<std::streamsize>(bucket_size));
        }

        if (!in) {
            throw std::runtime_error("CuckooFilter::deserialize: read failed");
        }

        return filter;
    }

private:
    CuckooFilter(std::size_t num_buckets, std::size_t bucket_size, std::size_t fingerprint_bits,
                 std::size_t count, Hash hasher)
        : buckets_(num_buckets, std::vector<std::uint8_t>(bucket_size, 0)),
          num_buckets_(num_buckets),
          bucket_size_(bucket_size),
          fingerprint_bits_(fingerprint_bits),
          bucket_mask_(num_buckets - 1),
          fingerprint_mask_(static_cast<std::uint8_t>((1u << fingerprint_bits) - 1)),
          count_(count),
          rng_state_(0xdeadbeefcafe1234ULL),
          hasher_(std::move(hasher))
    {}

    static constexpr std::size_t kMaxKicks = 500;

    static constexpr std::uint64_t fmix64(std::uint64_t k) noexcept {
        k ^= k >> 33;
        k *= 0xff51afd7ed558ccdULL;
        k ^= k >> 33;
        k *= 0xc4ceb9fe1a85ec53ULL;
        k ^= k >> 33;
        return k;
    }

    [[nodiscard]] std::uint64_t full_hash(const T& key) const noexcept {
        return hasher_(key);
    }

    [[nodiscard]] std::uint8_t fingerprint(std::uint64_t h) const noexcept {
        auto fp = static_cast<std::uint8_t>(h & fingerprint_mask_);
        if (fp == 0) fp = 1;
        return fp;
    }

    [[nodiscard]] std::size_t index_hash(std::uint64_t h) const noexcept {
        return static_cast<std::size_t>((h >> 32) & bucket_mask_);
    }

    [[nodiscard]] std::size_t alt_index(std::size_t i, std::uint8_t fp) const noexcept {
        const std::uint64_t fp_hash = fmix64(static_cast<std::uint64_t>(fp));
        return (i ^ static_cast<std::size_t>(fp_hash)) & bucket_mask_;
    }

    bool insert_to_bucket(std::size_t bucket_idx, std::uint8_t fp) {
        auto& bucket = buckets_[bucket_idx];
        for (std::size_t s = 0; s < bucket_size_; ++s) {
            if (bucket[s] == 0) {
                bucket[s] = fp;
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool bucket_contains(std::size_t bucket_idx, std::uint8_t fp) const noexcept {
        const auto& bucket = buckets_[bucket_idx];
        for (std::size_t s = 0; s < bucket_size_; ++s) {
            if (bucket[s] == fp) {
                return true;
            }
        }
        return false;
    }

    bool remove_from_bucket(std::size_t bucket_idx, std::uint8_t fp) {
        auto& bucket = buckets_[bucket_idx];
        for (std::size_t s = 0; s < bucket_size_; ++s) {
            if (bucket[s] == fp) {
                bucket[s] = 0;
                return true;
            }
        }
        return false;
    }

    std::uint64_t xorshift64() noexcept {
        std::uint64_t x = rng_state_;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        rng_state_ = x;
        return x;
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

    static void write_u64(std::ostream& out, std::uint64_t v) {
        out.write(reinterpret_cast<const char*>(&v), sizeof(v));
    }
    static std::uint64_t read_u64(std::istream& in) {
        std::uint64_t v;
        in.read(reinterpret_cast<char*>(&v), sizeof(v));
        return v;
    }

    std::vector<std::vector<std::uint8_t>> buckets_;
    std::size_t num_buckets_;
    std::size_t bucket_size_;
    std::size_t fingerprint_bits_;
    std::size_t bucket_mask_;
    std::uint8_t fingerprint_mask_;
    std::size_t count_;
    std::uint64_t rng_state_;
    Hash hasher_;
};

} // namespace probds
