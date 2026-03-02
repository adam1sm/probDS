#pragma once
// =============================================================================
// ribbon_filter.hpp — Bumped Ribbon Static Filter (BuRR variant)
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

// Helper function defined before use to satisfy constexpr constraints
static constexpr std::array<uint64_t, 256> make_ribbon_expand_table() {
    std::array<uint64_t, 256> table{};
    for (int i = 0; i < 256; ++i) {
        uint64_t val = 0;
        for (int j = 0; j < 8; ++j) {
            if ((i >> j) & 1) {
                val |= (0xFFULL << (j * 8));
            }
        }
        table[i] = val;
    }
    return table;
}

struct RibbonExpandTable {
    static constexpr std::array<uint64_t, 256> table = make_ribbon_expand_table();
};

template <typename T = std::string_view, typename FingerprintType = std::uint8_t, typename CoeffRow = std::uint64_t, typename Hash = DefaultHash<T>>
class RibbonFilter {
public:
    static_assert(is_hash_policy_v<Hash, T>, "Hash must satisfy the HashPolicy concept");
    static_assert(sizeof(FingerprintType) == 1 || sizeof(FingerprintType) == 2, 
                  "FingerprintType must be 8-bit (uint8_t) or 16-bit (uint16_t)");
    static_assert(sizeof(CoeffRow) == 8 || sizeof(CoeffRow) == 16, 
                  "CoeffRow must be 64-bit (uint64_t) or 128-bit (unsigned __int128)");

    // =========================================================================
    // Construction & Destruction
    // =========================================================================

    /// Empty constructor for deserialization
    explicit RibbonFilter(Hash hasher = Hash{})
        : seed_(0), size_(0), num_starts_(0), block_mask_(0), num_slots_(0), hasher_(std::move(hasher))
    {}

    /// Constructor populating the static filter with the entire dataset
    template <typename InputIt>
    RibbonFilter(InputIt first, InputIt last, Hash hasher = Hash{})
        : seed_(0), size_(0), num_starts_(0), block_mask_(0), num_slots_(0), hasher_(std::move(hasher))
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
            num_starts_ = 1;
            block_mask_ = 0;
            num_slots_ = CoeffBits;
            solution_.assign(num_slots_, 0);
            seed_ = 0;
            
            std::uint64_t hash = mix_split(hashed_keys[0], seed_);
            FingerprintType f = fingerprint(hash);
            solution_[0] = f;
            return;
        }

        // 3. Allocate structure
        double size_factor = (sizeof(CoeffRow) == 8) ? 1.08 : 1.05;
        num_starts_ = 32;
        while (num_starts_ + CoeffBits < size_factor * num_keys) {
            num_starts_ <<= 1;
        }
        block_mask_ = num_starts_ - 1;
        num_slots_ = num_starts_ + CoeffBits;
        solution_.assign(num_slots_, 0);
        size_ = num_keys;

        // 4. Populate with banding and back-substitution
        if (!populate(hashed_keys.data(), num_keys)) {
            clear();
            throw std::runtime_error("RibbonFilter: population failed (exceeded max iterations)");
        }
    }

    ~RibbonFilter() = default;

    RibbonFilter(const RibbonFilter&) = default;
    RibbonFilter& operator=(const RibbonFilter&) = default;
    RibbonFilter(RibbonFilter&&) noexcept = default;
    RibbonFilter& operator=(RibbonFilter&&) noexcept = default;

    // =========================================================================
    // Core Query Operations
    // =========================================================================

    /// Query membership of a key
    // TODO: Optimize lookup path
    [[nodiscard]] bool possibly_contains(const T& key) const noexcept {
        if (size_ == 0) return false;
        if (size_ == 1) {
            std::uint64_t hash = mix_split(hasher_(key), seed_);
            FingerprintType f = fingerprint(hash);
            return solution_[0] == f;
        }

        std::uint64_t hash = mix_split(hasher_(key), seed_);
        FingerprintType f = fingerprint(hash);
        CoeffRow coeff = get_coeff_row(hash);
        std::size_t start = hash & block_mask_;

        const FingerprintType* sol_ptr = solution_.data() + start;

        if constexpr (sizeof(FingerprintType) == 1) {
            const uint64_t* sol_u64 = reinterpret_cast<const uint64_t*>(sol_ptr);
            uint64_t combined = 0;
            constexpr std::size_t w = CoeffBits;
            constexpr std::size_t num_words = w / 8;
            #pragma clang loop unroll(full)
            for (std::size_t k = 0; k < num_words; ++k) {
                uint8_t mask8 = static_cast<uint8_t>(coeff >> (k * 8)) & 0xFF;
                combined ^= sol_u64[k] & RibbonExpandTable::table[mask8];
            }
            combined ^= combined >> 32;
            combined ^= combined >> 16;
            combined ^= combined >> 8;
            FingerprintType result = combined & 0xFF;
            return result == f;
        } else {
            auto coeff_bit = [coeff](std::size_t i, std::uint64_t) constexpr noexcept -> FingerprintType {
                return -static_cast<FingerprintType>((coeff >> i) & 1);
            };

            FingerprintType result = 0;
            constexpr std::size_t w = CoeffBits;
            #pragma clang loop unroll(full)
            for (std::size_t i = 0; i < w; ++i) {
                result ^= sol_ptr[i] & coeff_bit(i, hash);
            }
            return result == f;
        }
    }

    [[nodiscard]] bool contains(const T& key) const noexcept {
        return possibly_contains(key);
    }

    // =========================================================================
    // Introspection
    // =========================================================================

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    
    [[nodiscard]] std::size_t memory_bytes() const noexcept {
        return solution_.size() * sizeof(FingerprintType) + sizeof(RibbonFilter);
    }
    [[nodiscard]] std::size_t memory_usage() const noexcept { return memory_bytes(); }

    void clear() noexcept {
        solution_.clear();
        seed_ = 0;
        size_ = 0;
        num_starts_ = 0;
        block_mask_ = 0;
        num_slots_ = 0;
    }

    // =========================================================================
    // Serialization
    // =========================================================================

    void serialize(std::ostream& out) const {
        char magic[4] = {'P', 'R', 'F', (sizeof(FingerprintType) == 1 ? '1' : '2')};
        out.write(magic, 4);
        std::uint8_t coeff_row_size = sizeof(CoeffRow);
        out.write(reinterpret_cast<const char*>(&coeff_row_size), sizeof(coeff_row_size));
        out.write(reinterpret_cast<const char*>(&seed_), sizeof(seed_));
        out.write(reinterpret_cast<const char*>(&size_), sizeof(size_));
        out.write(reinterpret_cast<const char*>(&num_starts_), sizeof(num_starts_));
        out.write(reinterpret_cast<const char*>(&block_mask_), sizeof(block_mask_));
        out.write(reinterpret_cast<const char*>(&num_slots_), sizeof(num_slots_));
        std::uint64_t fingerprints_size = solution_.size();
        out.write(reinterpret_cast<const char*>(&fingerprints_size), sizeof(fingerprints_size));
        if (fingerprints_size > 0) {
            out.write(reinterpret_cast<const char*>(solution_.data()), 
                      static_cast<std::streamsize>(fingerprints_size * sizeof(FingerprintType)));
        }
        if (!out) {
            throw std::runtime_error("RibbonFilter::serialize: write failed");
        }
    }

    static RibbonFilter deserialize(std::istream& in, Hash hasher = Hash{}) {
        char magic[4];
        in.read(magic, 4);
        char expected_char = (sizeof(FingerprintType) == 1 ? '1' : '2');
        if (magic[0] != 'P' || magic[1] != 'R' || magic[2] != 'F' || magic[3] != expected_char) {
            throw std::runtime_error("RibbonFilter::deserialize: invalid magic number");
        }

        std::uint8_t coeff_row_size = 0;
        in.read(reinterpret_cast<char*>(&coeff_row_size), sizeof(coeff_row_size));
        if (coeff_row_size != sizeof(CoeffRow)) {
            throw std::runtime_error("RibbonFilter::deserialize: CoeffRow size mismatch");
        }

        RibbonFilter filter(std::move(hasher));
        in.read(reinterpret_cast<char*>(&filter.seed_), sizeof(filter.seed_));
        in.read(reinterpret_cast<char*>(&filter.size_), sizeof(filter.size_));
        in.read(reinterpret_cast<char*>(&filter.num_starts_), sizeof(filter.num_starts_));
        in.read(reinterpret_cast<char*>(&filter.block_mask_), sizeof(filter.block_mask_));
        in.read(reinterpret_cast<char*>(&filter.num_slots_), sizeof(filter.num_slots_));
        std::uint64_t fingerprints_size = 0;
        in.read(reinterpret_cast<char*>(&fingerprints_size), sizeof(fingerprints_size));

        if (fingerprints_size > 0) {
            filter.solution_.resize(fingerprints_size);
            in.read(reinterpret_cast<char*>(filter.solution_.data()), 
                    static_cast<std::streamsize>(fingerprints_size * sizeof(FingerprintType)));
        }

        if (!in) {
            throw std::runtime_error("RibbonFilter::deserialize: read failed");
        }
        return filter;
    }

private:
    static constexpr int MAX_CONSTRUCTION_ATTEMPTS = 10;
    static constexpr std::size_t CoeffBits = sizeof(CoeffRow) * 8;

    struct BandingEntry {
        CoeffRow coeff;
        FingerprintType target;
    };

    static inline int ctz(std::uint64_t val) noexcept {
        return __builtin_ctzll(val);
    }

#ifdef __SIZEOF_INT128__
    static inline int ctz(unsigned __int128 val) noexcept {
        std::uint64_t low = static_cast<std::uint64_t>(val);
        if (low != 0) {
            return __builtin_ctzll(low);
        }
        std::uint64_t high = static_cast<std::uint64_t>(val >> 64);
        return 64 + __builtin_ctzll(high);
    }
#endif

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

    CoeffRow get_coeff_row(std::uint64_t hash) const noexcept {
        if constexpr (sizeof(CoeffRow) == 8) {
            std::uint64_t rand_bits = hash * 0xff51afd7ed558ccdULL;
            return (rand_bits << 1) | 1ULL;
        } else {
            std::uint64_t rand_low = hash * 0xff51afd7ed558ccdULL;
            std::uint64_t rand_high = hash * 0xc4ceb9fe1a85ec53ULL;
            unsigned __int128 rand_bits = (static_cast<unsigned __int128>(rand_high) << 64) | rand_low;
            return (rand_bits << 1) | 1;
        }
    }

    FingerprintType fingerprint(std::uint64_t hash) const noexcept {
        return static_cast<FingerprintType>(hash >> (64 - sizeof(FingerprintType) * 8));
    }

    static inline std::uint64_t rng_splitmix64(std::uint64_t &seed) noexcept {
        std::uint64_t z = (seed += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31U);
    }

    bool populate(const std::uint64_t* keys, std::uint32_t size) {
        std::uint64_t rng_counter = 0x9e3779b97f4a7c15ULL;
        
        std::vector<BandingEntry> table(num_slots_);
        std::vector<bool> occupied(num_slots_, false);

        for (int attempt = 0; attempt < MAX_CONSTRUCTION_ATTEMPTS; ++attempt) {
            seed_ = rng_splitmix64(rng_counter);
            
            std::fill(occupied.begin(), occupied.end(), false);
            bool success = true;

            for (std::uint32_t i = 0; i < size; ++i) {
                std::uint64_t hash = mix_split(keys[i], seed_);
                FingerprintType target = fingerprint(hash);
                CoeffRow coeff = get_coeff_row(hash);
                std::size_t cur_start = hash & block_mask_;

                bool added = false;
                while (coeff != 0) {
                    int b = ctz(coeff);
                    std::size_t col = cur_start + b;
                    
                    if (!occupied[col]) {
                        table[col].coeff = coeff >> b;
                        table[col].target = target;
                        occupied[col] = true;
                        added = true;
                        break;
                    }
                    
                    // Eliminate
                    coeff = (coeff >> b) ^ table[col].coeff;
                    target = target ^ table[col].target;
                    cur_start = col;
                }

                if (!added) {
                    if (target != 0) {
                        success = false;
                        break; // Inconsistent system
                    }
                }
            }

            if (success) {
                // Solve using back-substitution
                solution_.assign(num_slots_, 0);
                for (std::size_t i = num_slots_ - 1; i < num_slots_; --i) {
                    if (occupied[i]) {
                        FingerprintType sum = 0;
                        CoeffRow coeff = table[i].coeff;
                        coeff >>= 1; // Skip bit 0 (representing column i)
                        std::size_t idx = i + 1;
                        while (coeff != 0) {
                            if (coeff & 1) {
                                sum ^= solution_[idx];
                            }
                            coeff >>= 1;
                            idx++;
                        }
                        solution_[i] = table[i].target ^ sum;
                    } else {
                        solution_[i] = 0;
                    }
                }
                return true;
            }
        }
        return false;
    }

    std::uint64_t seed_;
    std::uint32_t size_;
    std::uint32_t num_starts_;
    std::uint32_t block_mask_;
    std::uint32_t num_slots_;
    std::vector<FingerprintType> solution_;
    Hash hasher_;
};

} // namespace probds
