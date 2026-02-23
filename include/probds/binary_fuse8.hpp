#pragma once
// =============================================================================
// binary_fuse8.hpp — 3-way Binary Fuse Static Filter (8-bit fingerprints)
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

template <typename T = std::string_view, typename Hash = DefaultHash<T>>
class BinaryFuse8 {
public:
    static_assert(is_hash_policy_v<Hash, T>, "Hash must satisfy the HashPolicy concept");

    // =========================================================================
    // Construction & Destruction
    // =========================================================================

    /// Empty constructor for deserialization
    explicit BinaryFuse8(Hash hasher = Hash{})
        : seed_(0), size_(0), segment_length_(0), segment_length_mask_(0),
          segment_count_(0), segment_count_length_(0), array_length_(0),
          hasher_(std::move(hasher))
    {}

    /// Constructor populating the static filter with the entire dataset
    template <typename InputIt>
    BinaryFuse8(InputIt first, InputIt last, Hash hasher = Hash{})
        : seed_(0), size_(0), segment_length_(0), segment_length_mask_(0),
          segment_count_(0), segment_count_length_(0), array_length_(0),
          hasher_(std::move(hasher))
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
        if (num_keys < 2) {
            // Edge case: binary fuse filter expects at least 2 keys
            // If < 2 keys, we allocate a small default layout
            allocate(num_keys);
            if (num_keys == 1) {
                // Manually solve for 1 key
                std::uint64_t hash = mix_split(hashed_keys[0], seed_);
                std::uint8_t f = fingerprint(hash);
                std::uint32_t h0 = hash_pos(0, hash);
                fingerprints_[h0] = f;
            }
            return;
        }

        // 3. Allocate structure
        if (!allocate(num_keys)) {
            throw std::runtime_error("BinaryFuse8: allocation failed");
        }

        // 4. Populate with peeling algorithm
        if (!populate(hashed_keys.data(), num_keys)) {
            clear();
            throw std::runtime_error("BinaryFuse8: population failed (exceeded max iterations)");
        }
    }

    ~BinaryFuse8() = default;

    BinaryFuse8(const BinaryFuse8&) = default;
    BinaryFuse8& operator=(const BinaryFuse8&) = default;
    BinaryFuse8(BinaryFuse8&&) noexcept = default;
    BinaryFuse8& operator=(BinaryFuse8&&) noexcept = default;

    // =========================================================================
    // Core Query Operations
    // =========================================================================

    /// Query membership of a key
    [[nodiscard]] bool possibly_contains(const T& key) const noexcept {
        if (size_ == 0) return false;
        if (size_ == 1) {
            std::uint64_t hash = mix_split(hasher_(key), seed_);
            std::uint8_t f = fingerprint(hash);
            std::uint32_t h0 = hash_pos(0, hash);
            return fingerprints_[h0] == f;
        }

        std::uint64_t hash = mix_split(hasher_(key), seed_);
        std::uint8_t f = fingerprint(hash);

        std::uint64_t hi = mulhi(hash, segment_count_length_);
        std::uint32_t h0 = static_cast<std::uint32_t>(hi);
        std::uint32_t h1 = h0 + segment_length_;
        std::uint32_t h2 = h1 + segment_length_;
        h1 ^= static_cast<std::uint32_t>(hash >> 18U) & segment_length_mask_;
        h2 ^= static_cast<std::uint32_t>(hash) & segment_length_mask_;

        f ^= fingerprints_[h0] ^ fingerprints_[h1] ^ fingerprints_[h2];
        return f == 0;
    }

    [[nodiscard]] bool contains(const T& key) const noexcept {
        return possibly_contains(key);
    }

    // =========================================================================
    // Introspection
    // =========================================================================

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    
    [[nodiscard]] std::size_t memory_bytes() const noexcept {
        return fingerprints_.size() * sizeof(std::uint8_t) + sizeof(BinaryFuse8);
    }
    [[nodiscard]] std::size_t memory_usage() const noexcept { return memory_bytes(); }

    void clear() noexcept {
        fingerprints_.clear();
        seed_ = 0;
        size_ = 0;
        segment_length_ = 0;
        segment_length_mask_ = 0;
        segment_count_ = 0;
        segment_count_length_ = 0;
        array_length_ = 0;
    }

    // =========================================================================
    // Serialization
    // =========================================================================

    void serialize(std::ostream& out) const {
        out.write("PBF8", 4);
        out.write(reinterpret_cast<const char*>(&seed_), sizeof(seed_));
        out.write(reinterpret_cast<const char*>(&size_), sizeof(size_));
        out.write(reinterpret_cast<const char*>(&segment_length_), sizeof(segment_length_));
        out.write(reinterpret_cast<const char*>(&segment_length_mask_), sizeof(segment_length_mask_));
        out.write(reinterpret_cast<const char*>(&segment_count_), sizeof(segment_count_));
        out.write(reinterpret_cast<const char*>(&segment_count_length_), sizeof(segment_count_length_));
        out.write(reinterpret_cast<const char*>(&array_length_), sizeof(array_length_));
        if (array_length_ > 0) {
            out.write(reinterpret_cast<const char*>(fingerprints_.data()), static_cast<std::streamsize>(array_length_));
        }
        if (!out) {
            throw std::runtime_error("BinaryFuse8::serialize: write failed");
        }
    }

    static BinaryFuse8 deserialize(std::istream& in, Hash hasher = Hash{}) {
        char magic[4];
        in.read(magic, 4);
        if (std::memcmp(magic, "PBF8", 4) != 0) {
            throw std::runtime_error("BinaryFuse8::deserialize: invalid magic number");
        }

        BinaryFuse8 filter(std::move(hasher));
        in.read(reinterpret_cast<char*>(&filter.seed_), sizeof(filter.seed_));
        in.read(reinterpret_cast<char*>(&filter.size_), sizeof(filter.size_));
        in.read(reinterpret_cast<char*>(&filter.segment_length_), sizeof(filter.segment_length_));
        in.read(reinterpret_cast<char*>(&filter.segment_length_mask_), sizeof(filter.segment_length_mask_));
        in.read(reinterpret_cast<char*>(&filter.segment_count_), sizeof(filter.segment_count_));
        in.read(reinterpret_cast<char*>(&filter.segment_count_length_), sizeof(filter.segment_count_length_));
        in.read(reinterpret_cast<char*>(&filter.array_length_), sizeof(filter.array_length_));

        if (filter.array_length_ > 0) {
            filter.fingerprints_.resize(filter.array_length_);
            in.read(reinterpret_cast<char*>(filter.fingerprints_.data()), static_cast<std::streamsize>(filter.array_length_));
        }

        if (!in) {
            throw std::runtime_error("BinaryFuse8::deserialize: read failed");
        }
        return filter;
    }

private:
    static constexpr int XOR_MAX_ITERATIONS = 100;

    static inline std::uint64_t mulhi(std::uint64_t a, std::uint64_t b) noexcept {
#ifdef __SIZEOF_INT128__
        return static_cast<std::uint64_t>((static_cast<__uint128_t>(a) * b) >> 64U);
#else
        const std::uint64_t a0 = static_cast<std::uint32_t>(a);
        const std::uint64_t a1 = a >> 32;
        const std::uint64_t b0 = static_cast<std::uint32_t>(b);
        const std::uint64_t b1 = b >> 32;
        const std::uint64_t p11 = a1 * b1;
        const std::uint64_t p01 = a0 * b1;
        const std::uint64_t p10 = a1 * b0;
        const std::uint64_t p00 = a0 * b0;
        const std::uint64_t middle = p10 + (p00 >> 32) + static_cast<std::uint32_t>(p01);
        return p11 + (middle >> 32) + (p01 >> 32);
#endif
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

    static inline std::uint8_t fingerprint(std::uint64_t hash) noexcept {
        return static_cast<std::uint8_t>(hash ^ (hash >> 32U));
    }

    static inline std::uint64_t rng_splitmix64(std::uint64_t &seed) noexcept {
        std::uint64_t z = (seed += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31U);
    }

    static inline std::uint8_t mod3(std::uint8_t x) noexcept {
        return x > 2 ? x - 3 : x;
    }

    inline std::uint32_t hash_pos(std::uint64_t index, std::uint64_t hash) const noexcept {
        std::uint64_t h = mulhi(hash, segment_count_length_);
        h += index * segment_length_;
        std::uint64_t hh = hash & ((1ULL << 36U) - 1);
        h ^= static_cast<std::size_t>((hh >> (36 - 18 * index)) & segment_length_mask_);
        return static_cast<std::uint32_t>(h);
    }

    static inline std::uint32_t calculate_segment_length(std::uint32_t arity, std::uint32_t size) noexcept {
        if (arity == 3) {
            return std::uint32_t{1} << static_cast<unsigned>(std::floor(std::log(static_cast<double>(size)) / std::log(3.33) + 2.25));
        }
        return 65536;
    }

    static inline double calculate_size_factor(std::uint32_t arity, std::uint32_t size) noexcept {
        if (arity == 3) {
            return std::max(1.125, 0.875 + 0.25 * std::log(1000000.0) / std::log(static_cast<double>(size)));
        }
        return 2.0;
    }

    bool allocate(std::uint32_t size) {
        std::uint32_t arity = 3;
        size_ = size;
        segment_length_ = size == 0 ? 4 : calculate_segment_length(arity, size);
        if (segment_length_ > 262144) {
            segment_length_ = 262144;
        }
        segment_length_mask_ = segment_length_ - 1;
        double sizeFactor = size <= 1 ? 0 : calculate_size_factor(arity, size);
        std::uint32_t capacity = size <= 1 ? 0 : static_cast<std::uint32_t>(std::round(static_cast<double>(size) * sizeFactor));
        std::uint32_t initSegmentCount = (capacity + segment_length_ - 1) / segment_length_ - (arity - 1);
        array_length_ = (initSegmentCount + arity - 1) * segment_length_;
        segment_count_ = (array_length_ + segment_length_ - 1) / segment_length_;
        if (segment_count_ <= arity - 1) {
            segment_count_ = 1;
        } else {
            segment_count_ = segment_count_ - (arity - 1);
        }
        array_length_ = (segment_count_ + arity - 1) * segment_length_;
        segment_count_length_ = segment_count_ * segment_length_;

        fingerprints_.assign(array_length_, 0);
        return true;
    }

    bool populate(std::uint64_t* keys, std::uint32_t size) {
        if (size != size_) {
            return false;
        }

        std::uint64_t rng_counter = 0x726b2b9d438b9d4dULL;
        seed_ = rng_splitmix64(rng_counter);
        
        std::vector<std::uint64_t> reverseOrder(size + 1, 0);
        std::uint32_t capacity = array_length_;
        std::vector<std::uint32_t> alone(capacity);
        std::vector<std::uint8_t> t2count(capacity, 0);
        std::vector<std::uint8_t> reverseH(size);
        std::vector<std::uint64_t> t2hash(capacity, 0);

        std::uint32_t blockBits = 1;
        while ((std::uint32_t{1} << blockBits) < segment_count_) {
            blockBits += 1;
        }
        std::uint32_t block = (std::uint32_t{1} << blockBits);
        std::vector<std::uint32_t> startPos(block);
        std::uint32_t h012[5];

        reverseOrder[size] = 1;
        for (int loop = 0; true; ++loop) {
            if (loop + 1 > XOR_MAX_ITERATIONS) {
                return false;
            }

            for (std::uint32_t i = 0; i < block; i++) {
                startPos[i] = static_cast<std::uint32_t>((static_cast<std::uint64_t>(i) * size) >> blockBits);
            }

            std::uint64_t maskblock = block - 1;
            for (std::uint32_t i = 0; i < size; i++) {
                std::uint64_t hash = murmur64(keys[i] + seed_);
                std::uint64_t segment_index = hash >> (64 - blockBits);
                while (reverseOrder[startPos[segment_index]] != 0) {
                    segment_index++;
                    segment_index &= maskblock;
                }
                reverseOrder[startPos[segment_index]] = hash;
                startPos[segment_index]++;
            }
            int error = 0;
            std::uint32_t duplicates = 0;
            for (std::uint32_t i = 0; i < size; i++) {
                std::uint64_t hash = reverseOrder[i];
                std::uint32_t h0 = hash_pos(0, hash);
                t2count[h0] += 4;
                t2hash[h0] ^= hash;
                
                std::uint32_t h1 = hash_pos(1, hash);
                t2count[h1] += 4;
                t2count[h1] ^= 1U;
                t2hash[h1] ^= hash;
                
                std::uint32_t h2 = hash_pos(2, hash);
                t2count[h2] += 4;
                t2hash[h2] ^= hash;
                t2count[h2] ^= 2U;
                
                if ((t2hash[h0] & t2hash[h1] & t2hash[h2]) == 0) {
                    if (((t2hash[h0] == 0) && (t2count[h0] == 8))
                     || ((t2hash[h1] == 0) && (t2count[h1] == 8))
                     || ((t2hash[h2] == 0) && (t2count[h2] == 8))) {
                        duplicates += 1;
                        t2count[h0] -= 4;
                        t2hash[h0] ^= hash;
                        t2count[h1] -= 4;
                        t2count[h1] ^= 1U;
                        t2hash[h1] ^= hash;
                        t2count[h2] -= 4;
                        t2count[h2] ^= 2U;
                        t2hash[h2] ^= hash;
                    }
                }
                error = (t2count[h0] < 4) ? 1 : error;
                error = (t2count[h1] < 4) ? 1 : error;
                error = (t2count[h2] < 4) ? 1 : error;
            }
            if (error) {
                std::fill(reverseOrder.begin(), reverseOrder.end(), 0);
                std::fill(t2count.begin(), t2count.end(), 0);
                std::fill(t2hash.begin(), t2hash.end(), 0);
                seed_ = rng_splitmix64(rng_counter);
                continue;
            }

            // End of key addition
            std::uint32_t Qsize = 0;
            for (std::uint32_t i = 0; i < capacity; i++) {
                alone[Qsize] = i;
                Qsize += ((t2count[i] >> 2U) == 1) ? 1U : 0U;
            }
            std::uint32_t stacksize = 0;
            while (Qsize > 0) {
                Qsize--;
                std::uint32_t index = alone[Qsize];
                if ((t2count[index] >> 2U) == 1) {
                    std::uint64_t hash = t2hash[index];

                    h012[1] = hash_pos(1, hash);
                    h012[2] = hash_pos(2, hash);
                    h012[3] = hash_pos(0, hash);
                    h012[4] = h012[1];
                    std::uint8_t found = t2count[index] & 3U;
                    reverseH[stacksize] = found;
                    reverseOrder[stacksize] = hash;
                    stacksize++;
                    
                    std::uint32_t other_index1 = h012[found + 1];
                    alone[Qsize] = other_index1;
                    Qsize += ((t2count[other_index1] >> 2U) == 2 ? 1U : 0U);

                    t2count[other_index1] -= 4;
                    t2count[other_index1] ^= mod3(found + 1);
                    t2hash[other_index1] ^= hash;

                    std::uint32_t other_index2 = h012[found + 2];
                    alone[Qsize] = other_index2;
                    Qsize += ((t2count[other_index2] >> 2U) == 2 ? 1U : 0U);
                    t2count[other_index2] -= 4;
                    t2count[other_index2] ^= mod3(found + 2);
                    t2hash[other_index2] ^= hash;
                }
            }
            if (stacksize + duplicates == size) {
                size = stacksize;
                break;
            }
            if (duplicates > 0) {
                std::sort(keys, keys + size);
                size = static_cast<std::uint32_t>(std::unique(keys, keys + size) - keys);
            }
            std::fill(reverseOrder.begin(), reverseOrder.end(), 0);
            std::fill(t2count.begin(), t2count.end(), 0);
            std::fill(t2hash.begin(), t2hash.end(), 0);
            seed_ = rng_splitmix64(rng_counter);
        }

        for (std::uint32_t i = size - 1; i < size; i--) {
            std::uint64_t hash = reverseOrder[i];
            std::uint8_t xor2 = fingerprint(hash);
            std::uint8_t found = reverseH[i];
            h012[0] = hash_pos(0, hash);
            h012[1] = hash_pos(1, hash);
            h012[2] = hash_pos(2, hash);
            h012[3] = h012[0];
            h012[4] = h012[1];
            fingerprints_[h012[found]] = static_cast<std::uint8_t>(
                static_cast<std::uint32_t>(xor2) ^
                static_cast<std::uint32_t>(fingerprints_[h012[found + 1]]) ^
                static_cast<std::uint32_t>(fingerprints_[h012[found + 2]])
            );
        }
        return true;
    }

    std::uint64_t seed_;
    std::uint32_t size_;
    std::uint32_t segment_length_;
    std::uint32_t segment_length_mask_;
    std::uint32_t segment_count_;
    std::uint32_t segment_count_length_;
    std::uint32_t array_length_;
    std::vector<std::uint8_t> fingerprints_;
    Hash hasher_;
};

} // namespace probds
