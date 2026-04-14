#pragma once
// =============================================================================
// weighted_minhash.hpp — Consistent Weighted Sampling (ICWS) MinHash
// probDS: High-Performance Probabilistic Data Structures Library
// =============================================================================

#include "hash.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <istream>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace probds {

template <typename T = std::string_view, typename Hash = DefaultHash<T>>
class WeightedMinHash {
public:
    static_assert(is_hash_policy_v<Hash, T>, "Hash must satisfy the HashPolicy concept");

    struct Element {
        std::uint64_t key_hash = 0;
        std::int64_t t = 0;
    };

    // =========================================================================
    // Construction
    // =========================================================================

    explicit WeightedMinHash(std::size_t k, Hash hasher = Hash{})
        : k_(k),
          hasher_(std::move(hasher)),
          signature_(k),
          min_ln_a_(k, std::numeric_limits<double>::infinity())
    {
        if (k == 0) {
            throw std::invalid_argument("WeightedMinHash: k must be > 0");
        }
    }

    // =========================================================================
    // Core Operations
    // =========================================================================

    /// Insert an item with a weight
    void insert(const T& key, double weight) {
        if (weight <= 0.0) return;

        const std::uint64_t key_hash = hasher_(key);

        for (std::size_t c = 0; c < k_; ++c) {
            const std::uint64_t seed = key_hash ^ fmix64(static_cast<std::uint64_t>(c));
            LCG rng(seed);

            double u1 = rng.next_double();
            double u2 = rng.next_double();
            double u3 = rng.next_double();
            double u4 = rng.next_double();
            double u5 = rng.next_double();

            double r = -std::log(u1 * u2);
            double ln_c = std::log(-std::log(u3 * u4));
            double beta = u5;

            double vlog = std::log(weight);
            double t = std::floor((vlog / r) + beta);
            double ln_y = (t - beta) * r;
            double ln_a = ln_c - ln_y - r;

            if (ln_a < min_ln_a_[c]) {
                min_ln_a_[c] = ln_a;
                signature_[c].key_hash = key_hash;
                signature_[c].t = static_cast<std::int64_t>(t);
            }
        }
    }

    /// Merge another WeightedMinHash signature into this one
    void merge(const WeightedMinHash& other) {
        if (k_ != other.k_) {
            throw std::invalid_argument("WeightedMinHash::merge: signatures must have the same size");
        }
        for (std::size_t i = 0; i < k_; ++i) {
            if (other.min_ln_a_[i] < min_ln_a_[i]) {
                min_ln_a_[i] = other.min_ln_a_[i];
                signature_[i] = other.signature_[i];
            }
        }
    }

    /// Estimate Jaccard similarity between this sketch and another
    [[nodiscard]] double jaccard_similarity(const WeightedMinHash& other) const {
        if (k_ != other.k_) {
            throw std::invalid_argument("WeightedMinHash::jaccard_similarity: signatures must have the same size");
        }
        std::size_t matches = 0;
        for (std::size_t i = 0; i < k_; ++i) {
            if (signature_[i].key_hash == other.signature_[i].key_hash &&
                signature_[i].t == other.signature_[i].t &&
                signature_[i].key_hash != 0) {
                ++matches;
            }
        }
        return static_cast<double>(matches) / static_cast<double>(k_);
    }

    /// Alias for jaccard_similarity to match expected API
    [[nodiscard]] double jaccard(const WeightedMinHash& other) const {
        return jaccard_similarity(other);
    }

    void clear() noexcept {
        std::fill(min_ln_a_.begin(), min_ln_a_.end(), std::numeric_limits<double>::infinity());
        for (auto& el : signature_) {
            el.key_hash = 0;
            el.t = 0;
        }
    }

    // =========================================================================
    // Statistics & Introspection
    // =========================================================================

    [[nodiscard]] std::size_t k() const noexcept { return k_; }
    
    [[nodiscard]] std::size_t memory_bytes() const noexcept {
        return sizeof(WeightedMinHash) + 
               signature_.size() * sizeof(Element) + 
               min_ln_a_.size() * sizeof(double);
    }
    [[nodiscard]] std::size_t memory_usage() const noexcept { return memory_bytes(); }

    const std::vector<Element>& signature() const noexcept { return signature_; }

    // =========================================================================
    // Serialization
    // =========================================================================

    void serialize(std::ostream& out) const {
        out.write("PWMH", 4);
        write_u64(out, k_);
        for (std::size_t i = 0; i < k_; ++i) {
            write_u64(out, signature_[i].key_hash);
            write_i64(out, signature_[i].t);
            write_double(out, min_ln_a_[i]);
        }
        if (!out) {
            throw std::runtime_error("WeightedMinHash::serialize: write failed");
        }
    }

    static WeightedMinHash deserialize(std::istream& in, Hash hasher = Hash{}) {
        char magic[4];
        in.read(magic, 4);
        if (std::memcmp(magic, "PWMH", 4) != 0) {
            throw std::runtime_error("WeightedMinHash::deserialize: invalid magic number");
        }

        const auto k = read_u64(in);
        WeightedMinHash wmh(k, std::move(hasher));
        for (std::size_t i = 0; i < k; ++i) {
            wmh.signature_[i].key_hash = read_u64(in);
            wmh.signature_[i].t = read_i64(in);
            wmh.min_ln_a_[i] = read_double(in);
        }

        if (!in) {
            throw std::runtime_error("WeightedMinHash::deserialize: read failed");
        }
        return wmh;
    }

private:
    struct LCG {
        std::uint64_t state;
        explicit LCG(std::uint64_t seed) : state(seed) {}
        std::uint64_t next() noexcept {
            state = state * 6364136223846793005ULL + 1442695040888963407ULL;
            return state;
        }
        double next_double() noexcept {
            std::uint64_t val = next();
            double d = static_cast<double>(val) / static_cast<double>(UINT64_MAX);
            if (d <= 0.0) d = 1e-15;
            if (d >= 1.0) d = 1.0 - 1e-15;
            return d;
        }
    };

    static constexpr std::uint64_t fmix64(std::uint64_t k) noexcept {
        k ^= k >> 33;
        k *= 0xff51afd7ed558ccdULL;
        k ^= k >> 33;
        k *= 0xc4ceb9fe1a85ec53ULL;
        k ^= k >> 33;
        return k;
    }

    static void write_u64(std::ostream& out, std::uint64_t v) {
        out.write(reinterpret_cast<const char*>(&v), sizeof(v));
    }

    static std::uint64_t read_u64(std::istream& in) {
        std::uint64_t v;
        in.read(reinterpret_cast<char*>(&v), sizeof(v));
        return v;
    }

    static void write_i64(std::ostream& out, std::int64_t v) {
        out.write(reinterpret_cast<const char*>(&v), sizeof(v));
    }

    static std::int64_t read_i64(std::istream& in) {
        std::int64_t v;
        in.read(reinterpret_cast<char*>(&v), sizeof(v));
        return v;
    }

    static void write_double(std::ostream& out, double v) {
        out.write(reinterpret_cast<const char*>(&v), sizeof(v));
    }

    static double read_double(std::istream& in) {
        double v;
        in.read(reinterpret_cast<char*>(&v), sizeof(v));
        return v;
    }

    std::size_t k_;
    Hash hasher_;
    std::vector<Element> signature_;
    std::vector<double> min_ln_a_;
};

} // namespace probds
