#pragma once
// =============================================================================
// xor_filter.hpp — 3-way Static XOR Filter (8-bit and 16-bit fingerprints)
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
#include <vector>

namespace probds {

template <typename T = std::string_view, typename FingerprintType = std::uint8_t, typename Hash = DefaultHash<T>>
class XorFilter {
public:
    static_assert(is_hash_policy_v<Hash, T>, "Hash must satisfy the HashPolicy concept");
    static_assert(sizeof(FingerprintType) == 1 || sizeof(FingerprintType) == 2, 
                  "FingerprintType must be 8-bit (uint8_t) or 16-bit (uint16_t)");

    // =========================================================================
    // Construction & Destruction
    // =========================================================================

    /// Empty constructor for deserialization
    explicit XorFilter(Hash hasher = Hash{})
        : seed_(0), size_(0), block_length_(0), block_mask_(0), hasher_(std::move(hasher))
    {}

    /// Constructor populating the static filter with the entire dataset
    template <typename InputIt>
    XorFilter(InputIt first, InputIt last, Hash hasher = Hash{})
        : seed_(0), size_(0), block_length_(0), block_mask_(0), hasher_(std::move(hasher))
    {
        // 1. Hash keys
        std::vector<std::uint64_t> hashed_keys;
        for (auto it = first; it != last; ++it) {
            hashed_keys.push_back(hasher_(*it));
        }

        // 2. Sort and remove duplicates to guarantee construction success
        std::sort(hashed_keys.begin(), hashed_keys.end());
        hashed_keys.erase(std::unique(hashed_keys.begin(), hashed_keys.end()), hashed_keys.end());

        std::uint32_t num_keys = static_cast<std::uint32_t>(hashed_keys.size());
        if (num_keys == 0) {
            return;
        }

        // Edge case: single element
        if (num_keys == 1) {
            size_ = 1;
            block_length_ = 1;
            block_mask_ = 0;
            fingerprints_.assign(3, 0);
            seed_ = 0;
            
            std::uint64_t hash = mix_split(hashed_keys[0], seed_);
            FingerprintType f = fingerprint(hash);
            fingerprints_[0] = f;
            return;
        }

        // 3. Allocate structure (3 blocks, each size of a power of 2)
        block_length_ = 32;
        while (3 * block_length_ < 1.23 * num_keys) {
            block_length_ <<= 1;
        }
        block_mask_ = block_length_ - 1;
        fingerprints_.assign(3 * block_length_, 0);
        size_ = num_keys;

        // 4. Populate with peeling algorithm
        if (!populate(hashed_keys.data(), num_keys)) {
            clear();
            throw std::runtime_error("XorFilter: population failed (exceeded max iterations)");
        }
    }

    ~XorFilter() = default;

    XorFilter(const XorFilter&) = default;
    XorFilter& operator=(const XorFilter&) = default;
    XorFilter(XorFilter&&) noexcept = default;
    XorFilter& operator=(XorFilter&&) noexcept = default;

    // =========================================================================
    // Core Query Operations
    // =========================================================================

    /// Query membership of a key
    [[nodiscard]] bool possibly_contains(const T& key) const noexcept {
        if (size_ == 0) return false;
        if (size_ == 1) {
            std::uint64_t hash = mix_split(hasher_(key), seed_);
            FingerprintType f = fingerprint(hash);
            return fingerprints_[0] == f;
        }

        std::uint64_t hash = mix_split(hasher_(key), seed_);
        FingerprintType f = fingerprint(hash);

        std::uint32_t h0 = hash & block_mask_;
        std::uint32_t h1 = (rotl(hash, 21) & block_mask_) + block_length_;
        std::uint32_t h2 = (rotl(hash, 42) & block_mask_) + 2 * block_length_;

        return f == (fingerprints_[h0] ^ fingerprints_[h1] ^ fingerprints_[h2]);
    }

    [[nodiscard]] bool contains(const T& key) const noexcept {
        return possibly_contains(key);
    }

    // =========================================================================
    // Introspection
    // =========================================================================

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    
    [[nodiscard]] std::size_t memory_bytes() const noexcept {
        return fingerprints_.size() * sizeof(FingerprintType) + sizeof(XorFilter);
    }
    [[nodiscard]] std::size_t memory_usage() const noexcept { return memory_bytes(); }

    void clear() noexcept {
        fingerprints_.clear();
        seed_ = 0;
        size_ = 0;
        block_length_ = 0;
        block_mask_ = 0;
    }

    // =========================================================================
    // Serialization
    // =========================================================================

    void serialize(std::ostream& out) const {
        char magic[4] = {'P', 'X', 'F', (sizeof(FingerprintType) == 1 ? '1' : '2')};
        out.write(magic, 4);
        out.write(reinterpret_cast<const char*>(&seed_), sizeof(seed_));
        out.write(reinterpret_cast<const char*>(&size_), sizeof(size_));
        out.write(reinterpret_cast<const char*>(&block_length_), sizeof(block_length_));
        out.write(reinterpret_cast<const char*>(&block_mask_), sizeof(block_mask_));
        std::uint64_t fingerprints_size = fingerprints_.size();
        out.write(reinterpret_cast<const char*>(&fingerprints_size), sizeof(fingerprints_size));
        if (fingerprints_size > 0) {
            out.write(reinterpret_cast<const char*>(fingerprints_.data()), 
                      static_cast<std::streamsize>(fingerprints_size * sizeof(FingerprintType)));
        }
        if (!out) {
            throw std::runtime_error("XorFilter::serialize: write failed");
        }
    }

    static XorFilter deserialize(std::istream& in, Hash hasher = Hash{}) {
        char magic[4];
        in.read(magic, 4);
        char expected_char = (sizeof(FingerprintType) == 1 ? '1' : '2');
        if (magic[0] != 'P' || magic[1] != 'X' || magic[2] != 'F' || magic[3] != expected_char) {
            throw std::runtime_error("XorFilter::deserialize: invalid magic number");
        }

        XorFilter filter(std::move(hasher));
        in.read(reinterpret_cast<char*>(&filter.seed_), sizeof(filter.seed_));
        in.read(reinterpret_cast<char*>(&filter.size_), sizeof(filter.size_));
        in.read(reinterpret_cast<char*>(&filter.block_length_), sizeof(filter.block_length_));
        in.read(reinterpret_cast<char*>(&filter.block_mask_), sizeof(filter.block_mask_));
        std::uint64_t fingerprints_size = 0;
        in.read(reinterpret_cast<char*>(&fingerprints_size), sizeof(fingerprints_size));

        if (fingerprints_size > 0) {
            filter.fingerprints_.resize(fingerprints_size);
            in.read(reinterpret_cast<char*>(filter.fingerprints_.data()), 
                    static_cast<std::streamsize>(fingerprints_size * sizeof(FingerprintType)));
        }

        if (!in) {
            throw std::runtime_error("XorFilter::deserialize: read failed");
        }
        return filter;
    }

private:
    static constexpr int MAX_CONSTRUCTION_ATTEMPTS = 10;

    static inline std::uint64_t rotl(std::uint64_t x, int r) noexcept {
        return (x << r) | (x >> (64 - r));
    }

    static inline std::uint64_t murmur64(std::uint64_t h) noexcept {
        h ^= h >> 33U;
        h *= 0xff51afd7ed558ccdULL;
        h ^= h >> 33U;
        h *= 0xc4ceb9fe1a85ec53ULL;
        h ^= h >> 33U;
        return h;
    }

    static inline std::uint64_t mix_split(std::uint64_t key, std::uint64_t seed) noexcept {
        return murmur64(key + seed);
    }

    static inline FingerprintType fingerprint(std::uint64_t hash) noexcept {
        return static_cast<FingerprintType>(hash ^ (hash >> 32U));
    }

    static inline std::uint64_t rng_splitmix64(std::uint64_t &seed) noexcept {
        std::uint64_t z = (seed += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31U);
    }

    bool populate(const std::uint64_t* keys, std::uint32_t size) {
        std::uint64_t rng_counter = 0x123456789ULL;
        
        std::vector<std::uint8_t> degrees(3 * block_length_);
        std::vector<std::uint64_t> xor_sums(3 * block_length_);
        
        std::vector<std::uint32_t> queue(3 * block_length_);
        std::vector<std::uint32_t> peeled_slots(size);
        std::vector<std::uint64_t> peeled_hashes(size);

        for (int attempt = 0; attempt < MAX_CONSTRUCTION_ATTEMPTS; ++attempt) {
            seed_ = rng_splitmix64(rng_counter);

            std::fill(degrees.begin(), degrees.end(), 0);
            std::fill(xor_sums.begin(), xor_sums.end(), 0);

            // 1. Build hypergraph
            for (std::uint32_t i = 0; i < size; ++i) {
                std::uint64_t hash = mix_split(keys[i], seed_);
                std::uint32_t h0 = hash & block_mask_;
                std::uint32_t h1 = (rotl(hash, 21) & block_mask_) + block_length_;
                std::uint32_t h2 = (rotl(hash, 42) & block_mask_) + 2 * block_length_;

                degrees[h0]++;
                xor_sums[h0] ^= hash;

                degrees[h1]++;
                xor_sums[h1] ^= hash;

                degrees[h2]++;
                xor_sums[h2] ^= hash;
            }

            // 2. Initialize queue with degree-1 slots
            std::uint32_t head = 0;
            std::uint32_t tail = 0;
            for (std::uint32_t i = 0; i < 3 * block_length_; ++i) {
                if (degrees[i] == 1) {
                    queue[tail++] = i;
                }
            }

            // 3. Peeling
            std::uint32_t peeled_count = 0;
            while (head < tail) {
                std::uint32_t slot = queue[head++];
                if (degrees[slot] != 1) {
                    continue; // Degree changed during processing
                }

                std::uint64_t hash = xor_sums[slot];
                peeled_slots[peeled_count] = slot;
                peeled_hashes[peeled_count] = hash;
                peeled_count++;

                // Retrieve neighbors
                std::uint32_t h0 = hash & block_mask_;
                std::uint32_t h1 = (rotl(hash, 21) & block_mask_) + block_length_;
                std::uint32_t h2 = (rotl(hash, 42) & block_mask_) + 2 * block_length_;

                auto peel_neighbor = [&](std::uint32_t neighbor) {
                    degrees[neighbor]--;
                    xor_sums[neighbor] ^= hash;
                    if (degrees[neighbor] == 1) {
                        queue[tail++] = neighbor;
                    }
                };

                if (slot == h0) {
                    peel_neighbor(h1);
                    peel_neighbor(h2);
                } else if (slot == h1) {
                    peel_neighbor(h0);
                    peel_neighbor(h2);
                } else {
                    peel_neighbor(h0);
                    peel_neighbor(h1);
                }
            }

            // 4. Back-substitution
            if (peeled_count == size) {
                for (std::uint32_t i = size; i > 0; --i) {
                    std::uint32_t peeled_index = i - 1;
                    std::uint32_t slot = peeled_slots[peeled_index];
                    std::uint64_t hash = peeled_hashes[peeled_index];

                    std::uint32_t h0 = hash & block_mask_;
                    std::uint32_t h1 = (rotl(hash, 21) & block_mask_) + block_length_;
                    std::uint32_t h2 = (rotl(hash, 42) & block_mask_) + 2 * block_length_;

                    FingerprintType f = fingerprint(hash);
                    if (slot == h0) {
                        fingerprints_[h0] = f ^ fingerprints_[h1] ^ fingerprints_[h2];
                    } else if (slot == h1) {
                        fingerprints_[h1] = f ^ fingerprints_[h0] ^ fingerprints_[h2];
                    } else {
                        fingerprints_[h2] = f ^ fingerprints_[h0] ^ fingerprints_[h1];
                    }
                }
                return true;
            }
        }
        return false;
    }

    std::uint64_t seed_;
    std::uint32_t size_;
    std::uint32_t block_length_;
    std::uint32_t block_mask_;
    std::vector<FingerprintType> fingerprints_;
    Hash hasher_;
};

} // namespace probds
