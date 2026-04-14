#pragma once
// =============================================================================
// bbit_minhash.hpp — b-Bit MinHash Signature Generator
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
class BBitMinHash {
public:
    static_assert(is_hash_policy_v<Hash, T>, "Hash must satisfy the HashPolicy concept");

    // =========================================================================
    // Construction
    // =========================================================================

    BBitMinHash(std::size_t k, std::size_t b, Hash hasher = Hash{})
        : count_(0),
          k_(k),
          b_(b),
          hasher_(std::move(hasher)),
          signature_(k, std::numeric_limits<std::uint64_t>::max())
    {
        if (k == 0) {
            throw std::invalid_argument("BBitMinHash: k must be > 0");
        }
        if (b < 1 || b > 8) {
            throw std::invalid_argument("BBitMinHash: b must be in [1, 8]");
        }
    }

    // =========================================================================
    // Core Operations
    // =========================================================================

    /// Insert an item into the MinHash signature
    void insert(const T& key) {
        const auto [h1, h2] = get_hash_pair(key);
        for (std::size_t i = 0; i < signature_.size(); ++i) {
            const std::uint64_t hi = h1 + i * h2;
            signature_[i] = std::min(signature_[i], hi);
        }
        ++count_;
    }

    /// Merge another BBitMinHash signature into this one
    void merge(const BBitMinHash& other) {
        if (k_ != other.k_) {
            throw std::invalid_argument("BBitMinHash::merge: signatures must have the same size");
        }
        if (b_ != other.b_) {
            throw std::invalid_argument("BBitMinHash::merge: bit width b must match");
        }
        for (std::size_t i = 0; i < signature_.size(); ++i) {
            signature_[i] = std::min(signature_[i], other.signature_[i]);
        }
        count_ += other.count_;
    }

    /// Estimate Jaccard similarity using Ping Li's correction formula
    [[nodiscard]] double jaccard_similarity(const BBitMinHash& other) const {
        if (k_ != other.k_) {
            throw std::invalid_argument("BBitMinHash::jaccard_similarity: signatures must have the same size");
        }
        if (b_ != other.b_) {
            throw std::invalid_argument("BBitMinHash::jaccard_similarity: bit width b must match");
        }

        std::size_t matches = 0;
        const std::uint64_t mask = (1ULL << b_) - 1;

        for (std::size_t i = 0; i < k_; ++i) {
            // Extracted b-bit fingerprints
            std::uint64_t val1 = signature_[i] & mask;
            std::uint64_t val2 = other.signature_[i] & mask;

            if (signature_[i] != std::numeric_limits<std::uint64_t>::max() &&
                other.signature_[i] != std::numeric_limits<std::uint64_t>::max()) {
                if (val1 == val2) {
                    ++matches;
                }
            }
        }

        double pb = static_cast<double>(matches) / static_cast<double>(k_);
        double factor = 1.0 / static_cast<double>(1ULL << b_);
        double jaccard_est = (pb - factor) / (1.0 - factor);

        return std::max(0.0, jaccard_est);
    }

    /// Alias for jaccard_similarity to match expected API
    [[nodiscard]] double jaccard(const BBitMinHash& other) const {
        return jaccard_similarity(other);
    }

    void clear() noexcept {
        std::fill(signature_.begin(), signature_.end(), std::numeric_limits<std::uint64_t>::max());
        count_ = 0;
    }

    // =========================================================================
    // Statistics & Introspection
    // =========================================================================

    [[nodiscard]] std::size_t k() const noexcept { return k_; }
    [[nodiscard]] std::size_t b() const noexcept { return b_; }
    [[nodiscard]] std::size_t size() const noexcept { return count_; }

    [[nodiscard]] std::size_t memory_bytes() const noexcept {
        // Reports b-bit packed size in memory_bytes()
        std::size_t packed_bytes = (k_ * b_ + 7) / 8;
        return sizeof(BBitMinHash) + packed_bytes;
    }
    [[nodiscard]] std::size_t memory_usage() const noexcept { return memory_bytes(); }

    const std::vector<std::uint64_t>& signature() const noexcept { return signature_; }

    // =========================================================================
    // Serialization
    // =========================================================================

    void serialize(std::ostream& out) const {
        out.write("PBB1", 4);
        write_u64(out, k_);
        write_u64(out, b_);
        write_u64(out, count_);

        std::vector<std::uint8_t> packed = pack_signature();
        write_u64(out, packed.size());
        if (!packed.empty()) {
            out.write(reinterpret_cast<const char*>(packed.data()), packed.size());
        }
        if (!out) {
            throw std::runtime_error("BBitMinHash::serialize: write failed");
        }
    }

    static BBitMinHash deserialize(std::istream& in, Hash hasher = Hash{}) {
        char magic[4];
        in.read(magic, 4);
        if (std::memcmp(magic, "PBB1", 4) != 0) {
            throw std::runtime_error("BBitMinHash::deserialize: invalid magic number");
        }

        const auto k = read_u64(in);
        const auto b = read_u64(in);
        const auto count = read_u64(in);
        const auto packed_size = read_u64(in);

        std::vector<std::uint8_t> packed(packed_size);
        if (packed_size > 0) {
            in.read(reinterpret_cast<char*>(packed.data()), packed_size);
        }

        if (!in) {
            throw std::runtime_error("BBitMinHash::deserialize: read failed");
        }

        BBitMinHash bbit(k, b, std::move(hasher));
        bbit.count_ = count;

        for (std::size_t i = 0; i < k; ++i) {
            std::uint64_t val = 0;
            std::size_t bit_offset = i * b;
            for (std::size_t bit = 0; bit < b; ++bit) {
                std::size_t current_bit = bit_offset + bit;
                if ((packed[current_bit / 8] >> (current_bit % 8)) & 1) {
                    val |= (1ULL << bit);
                }
            }
            bbit.signature_[i] = val;
        }

        return bbit;
    }

private:
    std::vector<std::uint8_t> pack_signature() const {
        std::size_t num_bits = k_ * b_;
        std::size_t num_bytes = (num_bits + 7) / 8;
        std::vector<std::uint8_t> packed(num_bytes, 0);

        const std::uint64_t mask = (1ULL << b_) - 1;

        for (std::size_t i = 0; i < k_; ++i) {
            std::uint64_t val = 0;
            if (signature_[i] != std::numeric_limits<std::uint64_t>::max()) {
                val = signature_[i] & mask;
            }
            std::size_t bit_offset = i * b_;
            for (std::size_t bit = 0; bit < b_; ++bit) {
                if ((val >> bit) & 1) {
                    std::size_t current_bit = bit_offset + bit;
                    packed[current_bit / 8] |= (1 << (current_bit % 8));
                }
            }
        }
        return packed;
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

    static void write_u64(std::ostream& out, std::uint64_t v) {
        out.write(reinterpret_cast<const char*>(&v), sizeof(v));
    }

    static std::uint64_t read_u64(std::istream& in) {
        std::uint64_t v;
        in.read(reinterpret_cast<char*>(&v), sizeof(v));
        return v;
    }

    std::size_t count_;
    std::size_t k_;
    std::size_t b_;
    Hash hasher_;
    std::vector<std::uint64_t> signature_;
};

} // namespace probds
