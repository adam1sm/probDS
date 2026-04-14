#pragma once
// =============================================================================
// minhash.hpp — MinHash Signature Generator for Jaccard Similarity
// probDS: High-Performance Probabilistic Data Structures Library
// =============================================================================

#include "hash.hpp"
#include <algorithm>
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
class MinHash {
public:
    static_assert(is_hash_policy_v<Hash, T>, "Hash must satisfy the HashPolicy concept");

    // =========================================================================
    // Construction
    // =========================================================================

    explicit MinHash(std::size_t k, Hash hasher = Hash{})
        : count_(0), hasher_(std::move(hasher)), signature_(k, std::numeric_limits<std::uint64_t>::max())
    {
        if (k == 0) {
            throw std::invalid_argument("MinHash: k must be > 0");
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

    /// Merge another MinHash signature into this one
    void merge(const MinHash& other) {
        if (signature_.size() != other.signature_.size()) {
            throw std::invalid_argument("MinHash::merge: signatures must have the same size");
        }
        for (std::size_t i = 0; i < signature_.size(); ++i) {
            signature_[i] = std::min(signature_[i], other.signature_[i]);
        }
        count_ += other.count_;
    }

    /// Estimate Jaccard similarity between this MinHash and another
    [[nodiscard]] double jaccard_similarity(const MinHash& other) const {
        if (signature_.size() != other.signature_.size()) {
            throw std::invalid_argument("MinHash::jaccard_similarity: signatures must have the same size");
        }
        std::size_t matches = 0;
        for (std::size_t i = 0; i < signature_.size(); ++i) {
            if (signature_[i] == other.signature_[i] && signature_[i] != std::numeric_limits<std::uint64_t>::max()) {
                ++matches;
            }
        }
        return static_cast<double>(matches) / static_cast<double>(signature_.size());
    }

    // =========================================================================
    // Serialization
    // =========================================================================

    void serialize(std::ostream& out) const {
        out.write("PMH1", 4);
        write_u64(out, signature_.size());
        write_u64(out, count_);
        out.write(reinterpret_cast<const char*>(signature_.data()),
                  static_cast<std::streamsize>(signature_.size() * sizeof(std::uint64_t)));
        if (!out) {
            throw std::runtime_error("MinHash::serialize: write failed");
        }
    }

    static MinHash deserialize(std::istream& in, Hash hasher = Hash{}) {
        char magic[4];
        in.read(magic, 4);
        if (std::memcmp(magic, "PMH1", 4) != 0) {
            throw std::runtime_error("MinHash::deserialize: invalid magic number");
        }

        const auto k = read_u64(in);
        const auto count = read_u64(in);

        std::vector<std::uint64_t> sig(k);
        in.read(reinterpret_cast<char*>(sig.data()),
                static_cast<std::streamsize>(k * sizeof(std::uint64_t)));

        if (!in) {
            throw std::runtime_error("MinHash::deserialize: read failed");
        }

        MinHash mh(k, std::move(hasher));
        mh.signature_ = std::move(sig);
        mh.count_ = count;
        return mh;
    }

    // =========================================================================
    // Statistics & Introspection
    // =========================================================================

    [[nodiscard]] std::size_t k() const noexcept { return signature_.size(); }
    [[nodiscard]] std::size_t size() const noexcept { return count_; }
    [[nodiscard]] std::size_t memory_bytes() const noexcept {
        return sizeof(MinHash) + signature_.size() * sizeof(std::uint64_t);
    }

    const std::vector<std::uint64_t>& signature() const noexcept { return signature_; }

    void clear() noexcept {
        std::fill(signature_.begin(), signature_.end(), std::numeric_limits<std::uint64_t>::max());
        count_ = 0;
    }

private:
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
    Hash hasher_;
    std::vector<std::uint64_t> signature_;
};

} // namespace probds
