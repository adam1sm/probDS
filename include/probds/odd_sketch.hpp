#pragma once
// =============================================================================
// odd_sketch.hpp — Parity-based Symmetric Difference Estimator
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
class OddSketch {
public:
    static_assert(is_hash_policy_v<Hash, T>, "Hash must satisfy the HashPolicy concept");

    // =========================================================================
    // Construction
    // =========================================================================

    explicit OddSketch(std::size_t m, Hash hasher = Hash{})
        : hasher_(std::move(hasher))
    {
        if (m == 0) {
            throw std::invalid_argument("OddSketch: m must be > 0");
        }
        m_ = next_power_of_two(m < 64 ? 64 : m);
        m_mask_ = m_ - 1;
        bits_.assign(m_ / 64, 0ULL);
    }

    // =========================================================================
    // Core Operations
    // =========================================================================

    /// Insert an item (flips the mapped bit)
    void insert(const T& key) {
        const std::uint64_t hash_val = hasher_(key);
        const std::size_t idx = static_cast<std::size_t>(hash_val & m_mask_);
        const std::size_t word_idx = idx >> 6; // idx / 64
        const std::size_t bit_idx = idx & 63;  // idx % 64
        bits_[word_idx] ^= (1ULL << bit_idx);
    }

    /// Merge another OddSketch into this one (element-wise XOR)
    void merge(const OddSketch& other) {
        if (m_ != other.m_) {
            throw std::invalid_argument("OddSketch::merge: sketch sizes must match");
        }
        for (std::size_t i = 0; i < bits_.size(); ++i) {
            bits_[i] ^= other.bits_[i];
        }
    }

    /// Estimate the symmetric difference cardinality |A \Delta B|
    [[nodiscard]] double symmetric_difference(const OddSketch& other) const {
        if (m_ != other.m_) {
            throw std::invalid_argument("OddSketch::symmetric_difference: sketch sizes must match");
        }
        std::size_t z = 0;
        for (std::size_t i = 0; i < bits_.size(); ++i) {
            z += static_cast<std::size_t>(__builtin_popcountll(bits_[i] ^ other.bits_[i]));
        }

        if (z == 0) return 0.0;

        double ratio = 1.0 - 2.0 * static_cast<double>(z) / static_cast<double>(m_);
        if (ratio <= 0.0) {
            return std::numeric_limits<double>::infinity();
        }

        return std::log(ratio) / std::log(1.0 - 2.0 / static_cast<double>(m_));
    }

    /// Estimate the cardinality of the sketch itself
    [[nodiscard]] double cardinality() const noexcept {
        std::size_t z = 0;
        for (auto val : bits_) {
            z += static_cast<std::size_t>(__builtin_popcountll(val));
        }

        if (z == 0) return 0.0;

        double ratio = 1.0 - 2.0 * static_cast<double>(z) / static_cast<double>(m_);
        if (ratio <= 0.0) {
            return std::numeric_limits<double>::infinity();
        }

        return std::log(ratio) / std::log(1.0 - 2.0 / static_cast<double>(m_));
    }

    void clear() noexcept {
        std::fill(bits_.begin(), bits_.end(), 0ULL);
    }

    // =========================================================================
    // Statistics & Introspection
    // =========================================================================

    [[nodiscard]] std::size_t m() const noexcept { return m_; }

    [[nodiscard]] std::size_t memory_bytes() const noexcept {
        return sizeof(OddSketch) + bits_.size() * sizeof(std::uint64_t);
    }
    [[nodiscard]] std::size_t memory_usage() const noexcept { return memory_bytes(); }

    const std::vector<std::uint64_t>& bits() const noexcept { return bits_; }

    // =========================================================================
    // Serialization
    // =========================================================================

    void serialize(std::ostream& out) const {
        out.write("PODS", 4);
        write_u64(out, m_);
        write_u64(out, bits_.size());
        if (!bits_.empty()) {
            out.write(reinterpret_cast<const char*>(bits_.data()), bits_.size() * sizeof(std::uint64_t));
        }
        if (!out) {
            throw std::runtime_error("OddSketch::serialize: write failed");
        }
    }

    static OddSketch deserialize(std::istream& in, Hash hasher = Hash{}) {
        char magic[4];
        in.read(magic, 4);
        if (std::memcmp(magic, "PODS", 4) != 0) {
            throw std::runtime_error("OddSketch::deserialize: invalid magic number");
        }

        const auto m = read_u64(in);
        const auto bits_size = read_u64(in);

        OddSketch sketch(m, std::move(hasher));
        if (bits_size != sketch.bits_.size()) {
            throw std::runtime_error("OddSketch::deserialize: size mismatch");
        }

        if (bits_size > 0) {
            in.read(reinterpret_cast<char*>(sketch.bits_.data()), static_cast<std::streamsize>(bits_size * sizeof(std::uint64_t)));
        }

        if (!in) {
            throw std::runtime_error("OddSketch::deserialize: read failed");
        }

        return sketch;
    }

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

    static void write_u64(std::ostream& out, std::uint64_t v) {
        out.write(reinterpret_cast<const char*>(&v), sizeof(v));
    }

    static std::uint64_t read_u64(std::istream& in) {
        std::uint64_t v;
        in.read(reinterpret_cast<char*>(&v), sizeof(v));
        return v;
    }

    std::size_t m_;
    std::size_t m_mask_;
    Hash hasher_;
    std::vector<std::uint64_t> bits_;
};

} // namespace probds
