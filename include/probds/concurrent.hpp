#pragma once
// =============================================================================
// concurrent.hpp — Thread-safe Probabilistic Data Structures
// probDS: High-Performance Probabilistic Data Structures Library
// =============================================================================

#include "hash.hpp"
#include "bloom_filter.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

namespace probds {

// =============================================================================
// ConcurrentBloomFilter
// =============================================================================
template <typename T = std::string_view, typename Hash = DefaultHash<T>>
class ConcurrentBloomFilter {
public:
    static_assert(is_hash_policy_v<Hash, T>, "Hash must satisfy the HashPolicy concept");

    ConcurrentBloomFilter(std::size_t expected_insertions,
                          double false_positive_rate = 0.01,
                          std::size_t num_shards = 128,
                          Hash hasher = Hash{})
        : num_shards_(num_shards),
          hasher_(std::move(hasher)),
          mutexes_(num_shards)
    {
        if (num_shards == 0 || (num_shards & (num_shards - 1)) != 0) {
            throw std::invalid_argument("ConcurrentBloomFilter: num_shards must be a power of two");
        }
        std::size_t shard_expected = expected_insertions / num_shards;
        if (shard_expected == 0) shard_expected = 1;

        shards_.reserve(num_shards);
        for (std::size_t i = 0; i < num_shards; ++i) {
            shards_.emplace_back(shard_expected, false_positive_rate, hasher_);
        }
    }

    // Thread-safe types are non-copyable and non-movable
    ConcurrentBloomFilter(const ConcurrentBloomFilter&) = delete;
    ConcurrentBloomFilter& operator=(const ConcurrentBloomFilter&) = delete;
    ConcurrentBloomFilter(ConcurrentBloomFilter&&) = delete;
    ConcurrentBloomFilter& operator=(ConcurrentBloomFilter&&) = delete;

    void insert(const T& key) {
        std::uint64_t hash = hasher_(key);
        std::size_t shard_idx = hash & (num_shards_ - 1);
        std::lock_guard<std::mutex> lock(mutexes_[shard_idx]);
        shards_[shard_idx].insert(key);
    }

    [[nodiscard]] bool possibly_contains(const T& key) const noexcept {
        std::uint64_t hash = hasher_(key);
        std::size_t shard_idx = hash & (num_shards_ - 1);
        std::lock_guard<std::mutex> lock(mutexes_[shard_idx]);
        return shards_[shard_idx].possibly_contains(key);
    }

    void clear() noexcept {
        for (std::size_t i = 0; i < num_shards_; ++i) {
            std::lock_guard<std::mutex> lock(mutexes_[i]);
            shards_[i].clear();
        }
    }

    [[nodiscard]] std::size_t size() const noexcept {
        std::size_t total = 0;
        for (std::size_t i = 0; i < num_shards_; ++i) {
            std::lock_guard<std::mutex> lock(mutexes_[i]);
            total += shards_[i].size();
        }
        return total;
    }

    [[nodiscard]] std::size_t memory_bytes() const noexcept {
        std::size_t total = sizeof(ConcurrentBloomFilter);
        for (std::size_t i = 0; i < num_shards_; ++i) {
            std::lock_guard<std::mutex> lock(mutexes_[i]);
            total += shards_[i].memory_bytes();
        }
        return total;
    }

private:
    std::size_t num_shards_;
    Hash hasher_;
    mutable std::vector<std::mutex> mutexes_;
    std::vector<BloomFilter<T, Hash>> shards_;
};

// =============================================================================
// ConcurrentHyperLogLog
// =============================================================================
template <typename T = std::string_view, typename Hash = DefaultHash<T>>
class ConcurrentHyperLogLog {
public:
    static_assert(is_hash_policy_v<Hash, T>, "Hash must satisfy the HashPolicy concept");

    ConcurrentHyperLogLog(std::uint8_t precision = 14,
                          std::size_t num_shards = 64,
                          Hash hasher = Hash{})
        : precision_(precision),
          num_shards_(num_shards),
          hasher_(std::move(hasher)),
          mutexes_(num_shards)
    {
        if (precision < 4 || precision > 16) {
            throw std::invalid_argument("ConcurrentHyperLogLog: precision must be in [4, 16]");
        }
        if (num_shards == 0 || (num_shards & (num_shards - 1)) != 0) {
            throw std::invalid_argument("ConcurrentHyperLogLog: num_shards must be a power of two");
        }

        std::size_t m = std::size_t{1} << precision_;
        shards_.reserve(num_shards);
        for (std::size_t i = 0; i < num_shards; ++i) {
            shards_.emplace_back(m, 0);
        }

        for (int i = 0; i <= 64; ++i) {
            pow2_table_[i] = 1.0 / static_cast<double>(1ULL << i);
        }
        pow2_table_[64] = std::ldexp(1.0, -64);
    }

    ConcurrentHyperLogLog(const ConcurrentHyperLogLog&) = delete;
    ConcurrentHyperLogLog& operator=(const ConcurrentHyperLogLog&) = delete;
    ConcurrentHyperLogLog(ConcurrentHyperLogLog&&) = delete;
    ConcurrentHyperLogLog& operator=(ConcurrentHyperLogLog&&) = delete;

    void insert(const T& key) {
        const std::uint64_t hash = hasher_(key);
        std::size_t shard_idx = hash & (num_shards_ - 1);

        const std::size_t j = hash >> (64 - precision_);
        const std::uint64_t remaining = (hash << precision_) | std::uint64_t{1};
        const std::uint8_t rho = static_cast<std::uint8_t>(__builtin_clzll(remaining) + 1);

        std::lock_guard<std::mutex> lock(mutexes_[shard_idx]);
        if (rho > shards_[shard_idx][j]) {
            shards_[shard_idx][j] = rho;
        }
    }

    [[nodiscard]] std::uint64_t estimate() const {
        std::size_t m = std::size_t{1} << precision_;
        std::vector<std::uint8_t> temp_registers(m, 0);

        // Lock all shards sequentially to take a snapshot
        std::vector<std::unique_lock<std::mutex>> locks;
        locks.reserve(num_shards_);
        for (std::size_t i = 0; i < num_shards_; ++i) {
            locks.emplace_back(mutexes_[i]);
        }

        // Aggregate registers (XOR/max) across all shards
        for (std::size_t s = 0; s < num_shards_; ++s) {
            for (std::size_t j = 0; j < m; ++j) {
                temp_registers[j] = std::max(temp_registers[j], shards_[s][j]);
            }
        }

        // Unlock
        locks.clear();

        // Calculate cardinality estimation
        double sum = 0.0;
        std::size_t zero_registers = 0;
        for (std::size_t j = 0; j < m; ++j) {
            std::uint8_t val = temp_registers[j];
            sum += pow2_table_[val];
            if (val == 0) {
                ++zero_registers;
            }
        }

        const double m_dbl = static_cast<double>(m);
        const double alpha = alpha_m(m);
        double E = alpha * m_dbl * m_dbl / sum;
        if (E <= 2.5 * m_dbl && zero_registers > 0) {
            E = m_dbl * std::log(m_dbl / static_cast<double>(zero_registers));
        }
        return static_cast<std::uint64_t>(E + 0.5);
    }

    [[nodiscard]] double relative_error() const noexcept {
        const double m = static_cast<double>(std::size_t{1} << precision_);
        return 1.04 / std::sqrt(m);
    }

    void clear() noexcept {
        for (std::size_t s = 0; s < num_shards_; ++s) {
            std::lock_guard<std::mutex> lock(mutexes_[s]);
            std::fill(shards_[s].begin(), shards_[s].end(), std::uint8_t{0});
        }
    }

    [[nodiscard]] std::size_t memory_bytes() const noexcept {
        std::size_t total = sizeof(ConcurrentHyperLogLog);
        for (std::size_t s = 0; s < num_shards_; ++s) {
            std::lock_guard<std::mutex> lock(mutexes_[s]);
            total += shards_[s].size() * sizeof(std::uint8_t);
        }
        return total;
    }
    [[nodiscard]] std::size_t memory_usage() const noexcept { return memory_bytes(); }

private:
    static double alpha_m(std::size_t m) noexcept {
        switch (m) {
            case 16:  return 0.673;
            case 32:  return 0.697;
            case 64:  return 0.709;
            default:
                return 0.7213 / (1.0 + 1.079 / static_cast<double>(m));
        }
    }

    std::uint8_t precision_;
    std::size_t num_shards_;
    Hash hasher_;
    mutable std::vector<std::mutex> mutexes_;
    std::vector<std::vector<std::uint8_t>> shards_;
    double pow2_table_[65];
};

// =============================================================================
// ConcurrentCountMin
// =============================================================================
template <typename T = std::string_view, typename Hash = DefaultHash<T>>
class ConcurrentCountMin {
public:
    static_assert(is_hash_policy_v<Hash, T>, "Hash must satisfy the HashPolicy concept");

    ConcurrentCountMin(double epsilon = 0.01,
                       double delta = 0.01,
                       Hash hasher = Hash{})
        : hasher_(std::move(hasher))
    {
        if (epsilon <= 0.0 || epsilon >= 1.0) {
            throw std::invalid_argument("ConcurrentCountMin: epsilon must be in (0, 1)");
        }
        if (delta <= 0.0 || delta >= 1.0) {
            throw std::invalid_argument("ConcurrentCountMin: delta must be in (0, 1)");
        }

        const std::size_t base_width = static_cast<std::size_t>(std::ceil(std::exp(1.0) / epsilon));
        width_ = next_power_of_two(base_width < 2 ? 2 : base_width);
        width_mask_ = width_ - 1;

        depth_ = static_cast<std::size_t>(std::ceil(std::log(1.0 / delta)));
        if (depth_ == 0) depth_ = 1;

        table_ = std::vector<std::vector<std::atomic<std::int64_t>>>(depth_);
        for (std::size_t j = 0; j < depth_; ++j) {
            table_[j] = std::vector<std::atomic<std::int64_t>>(width_);
            for (std::size_t i = 0; i < width_; ++i) {
                table_[j][i].store(0, std::memory_order_relaxed);
            }
        }

        seeds_.resize(depth_);
        for (std::size_t j = 0; j < depth_; ++j) {
            seeds_[j] = fmix64(static_cast<std::uint64_t>(j + 1));
        }
    }

    ConcurrentCountMin(const ConcurrentCountMin&) = delete;
    ConcurrentCountMin& operator=(const ConcurrentCountMin&) = delete;
    ConcurrentCountMin(ConcurrentCountMin&&) = delete;
    ConcurrentCountMin& operator=(ConcurrentCountMin&&) = delete;

    // TODO: Resolve thread contention
    void insert(const T& key, std::int64_t delta = 1) {
        if (delta == 0) return;
        for (std::size_t j = 0; j < depth_; ++j) {
            std::size_t col = hash_for_row(j, key);
            table_[j][col].fetch_add(delta, std::memory_order_relaxed);
        }
    }

    [[nodiscard]] std::int64_t estimate(const T& key) const noexcept {
        std::int64_t result = std::numeric_limits<std::int64_t>::max();
        for (std::size_t j = 0; j < depth_; ++j) {
            std::size_t col = hash_for_row(j, key);
            result = std::min(result, table_[j][col].load(std::memory_order_relaxed));
        }
        return result;
    }

    void clear() noexcept {
        for (std::size_t j = 0; j < depth_; ++j) {
            for (std::size_t i = 0; i < width_; ++i) {
                table_[j][i].store(0, std::memory_order_relaxed);
            }
        }
    }

    [[nodiscard]] std::size_t width() const noexcept { return width_; }
    [[nodiscard]] std::size_t depth() const noexcept { return depth_; }

    [[nodiscard]] std::size_t memory_bytes() const noexcept {
        return depth_ * width_ * sizeof(std::atomic<std::int64_t>) + sizeof(ConcurrentCountMin);
    }
    [[nodiscard]] std::size_t memory_usage() const noexcept { return memory_bytes(); }

private:
    static constexpr std::uint64_t fmix64(std::uint64_t k) noexcept {
        k ^= k >> 33;
        k *= 0xff51afd7ed558ccdULL;
        k ^= k >> 33;
        k *= 0xc4ceb9fe1a85ec53ULL;
        k ^= k >> 33;
        return k;
    }

    [[nodiscard]] std::size_t hash_for_row(std::size_t row, const T& key) const noexcept {
        const std::uint64_t hash = hasher_(key) ^ seeds_[row];
        return static_cast<std::size_t>(fmix64(hash) & width_mask_);
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

    std::vector<std::vector<std::atomic<std::int64_t>>> table_;
    std::vector<std::uint64_t> seeds_;
    std::size_t width_;
    std::size_t width_mask_;
    std::size_t depth_;
    Hash hasher_;
};

} // namespace probds
