#pragma once
// =============================================================================
// simhash.hpp — SimHash Locality-Sensitive Hashing
// probDS: High-Performance Probabilistic Data Structures Library
// =============================================================================

#include "hash.hpp"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <istream>
#include <numeric>
#include <ostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace probds {

template <typename T = std::string_view, typename Hash = DefaultHash<T>>
class SimHash {
public:
    static_assert(is_hash_policy_v<Hash, T>, "Hash must satisfy the HashPolicy concept");

    // =========================================================================
    // Construction
    // =========================================================================

    explicit SimHash(Hash hasher = Hash{})
        : count_(0), hasher_(std::move(hasher)), v_(64, 0.0) {}

    // =========================================================================
    // Core Operations
    // =========================================================================

    /// Insert an item with a given weight (default 1.0)
    void insert(const T& key, double weight = 1.0) {
        if (weight <= 0.0) {
            throw std::invalid_argument("SimHash::insert: weight must be positive");
        }
        const std::uint64_t hash_val = hasher_(key);
        for (std::size_t i = 0; i < 64; ++i) {
            if ((hash_val >> i) & 1ULL) {
                v_[i] += weight;
            } else {
                v_[i] -= weight;
            }
        }
        ++count_;
    }

    /// Merge another SimHash sketch (element-wise addition of accumulators)
    void merge(const SimHash& other) {
        for (std::size_t i = 0; i < 64; ++i) {
            v_[i] += other.v_[i];
        }
        count_ += other.count_;
    }

    /// Compute the 64-bit fingerprint
    [[nodiscard]] std::uint64_t get_fingerprint() const noexcept {
        std::uint64_t fingerprint = 0;
        for (std::size_t i = 0; i < 64; ++i) {
            if (v_[i] > 0.0) {
                fingerprint |= (1ULL << i);
            }
        }
        return fingerprint;
    }

    /// Compute Hamming distance between this SimHash and another
    [[nodiscard]] std::size_t hamming_distance(const SimHash& other) const noexcept {
        const std::uint64_t fp1 = get_fingerprint();
        const std::uint64_t fp2 = other.get_fingerprint();
        return popcount(fp1 ^ fp2);
    }

    /// Estimate cosine similarity between this SimHash and another
    [[nodiscard]] double similarity(const SimHash& other) const noexcept {
        const std::size_t dist = hamming_distance(other);
        return similarity_from_distance(dist);
    }

    /// Static similarity estimator given two fingerprints
    [[nodiscard]] static double similarity(std::uint64_t fp1, std::uint64_t fp2) noexcept {
        const std::size_t dist = popcount(fp1 ^ fp2);
        return similarity_from_distance(dist);
    }

    // =========================================================================
    // Serialization
    // =========================================================================

    void serialize(std::ostream& out) const {
        out.write("PSH1", 4);
        write_u64(out, count_);
        out.write(reinterpret_cast<const char*>(v_.data()), 64 * sizeof(double));
        if (!out) {
            throw std::runtime_error("SimHash::serialize: write failed");
        }
    }

    static SimHash deserialize(std::istream& in, Hash hasher = Hash{}) {
        char magic[4];
        in.read(magic, 4);
        if (std::memcmp(magic, "PSH1", 4) != 0) {
            throw std::runtime_error("SimHash::deserialize: invalid magic number");
        }

        const auto count = read_u64(in);
        std::vector<double> v(64);
        in.read(reinterpret_cast<char*>(v.data()), 64 * sizeof(double));

        if (!in) {
            throw std::runtime_error("SimHash::deserialize: read failed");
        }

        SimHash sh(std::move(hasher));
        sh.count_ = count;
        sh.v_ = std::move(v);
        return sh;
    }

    // =========================================================================
    // Statistics & Introspection
    // =========================================================================

    [[nodiscard]] std::size_t size() const noexcept { return count_; }
    [[nodiscard]] std::size_t memory_bytes() const noexcept {
        return sizeof(SimHash) + v_.size() * sizeof(double);
    }

    const std::vector<double>& accumulators() const noexcept { return v_; }

    void clear() noexcept {
        std::fill(v_.begin(), v_.end(), 0.0);
        count_ = 0;
    }

private:
    static int popcount(std::uint64_t x) noexcept {
#if defined(__clang__) || defined(__GNUC__)
        return __builtin_popcountll(x);
#else
        x = x - ((x >> 1) & 0x5555555555555555ULL);
        x = (x & 0x3333333333333333ULL) + ((x >> 2) & 0x3333333333333333ULL);
        return (((x + (x >> 4)) & 0xF0F0F0F0F0F0F0FULL) * 0x101010101010101ULL) >> 56;
#endif
    }

    [[nodiscard]] static double similarity_from_distance(std::size_t dist) noexcept {
        // Cosine similarity = cos(pi * dist / 64)
        const double theta = (static_cast<double>(dist) / 64.0) * M_PI;
        return std::cos(theta);
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
    Hash hasher_;
    std::vector<double> v_;
};

} // namespace probds
