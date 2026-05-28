#pragma once
// =============================================================================
// count_min_log.hpp — Count-Min Sketch using Logarithmic Counters (Morris)
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

template <typename T = std::string_view, std::size_t CounterWidth = 4, typename Hash = DefaultHash<T>>
class CountMinLog {
public:
    static_assert(is_hash_policy_v<Hash, T>, "Hash must satisfy the HashPolicy concept");
    static_assert(CounterWidth == 4 || CounterWidth == 8, "CounterWidth must be 4 or 8");

    // =========================================================================
    // Construction
    // =========================================================================

    explicit CountMinLog(double epsilon = 0.01, double delta = 0.01, double base = (CounterWidth == 4 ? 2.0 : 1.082), Hash hasher = Hash{})
        : total_count_(0), base_(base), hasher_(std::move(hasher)), rng_(12345ULL)
    {
        if (epsilon <= 0.0 || epsilon >= 1.0) {
            throw std::invalid_argument("CountMinLog: epsilon must be in (0, 1)");
        }
        if (delta <= 0.0 || delta >= 1.0) {
            throw std::invalid_argument("CountMinLog: delta must be in (0, 1)");
        }
        if (base <= 1.0) {
            throw std::invalid_argument("CountMinLog: base must be > 1.0");
        }

        const std::size_t base_width = static_cast<std::size_t>(std::ceil(std::exp(1.0) / epsilon));
        width_ = next_power_of_two(base_width < 2 ? 2 : base_width);
        width_mask_ = width_ - 1;

        depth_ = static_cast<std::size_t>(std::ceil(std::log(1.0 / delta)));
        if (depth_ == 0) depth_ = 1;
        if (depth_ > 8) {
            throw std::invalid_argument("CountMinLog: depth must be <= 8");
        }

        table_.resize(depth_);
        for (std::size_t j = 0; j < depth_; ++j) {
            table_[j] = LogCounterStore(width_);
        }

        seeds_.resize(depth_);
        for (std::size_t j = 0; j < depth_; ++j) {
            seeds_[j] = fmix64(static_cast<std::uint64_t>(j + 1));
        }

        prob_table_.resize(1 << CounterWidth);
        for (size_t i = 0; i < prob_table_.size(); ++i) {
            prob_table_[i] = std::pow(base_, -static_cast<double>(i));
        }

        est_table_.resize(1 << CounterWidth);
        est_table_[0] = 0;
        for (std::size_t i = 1; i < est_table_.size(); ++i) {
            double est = (std::pow(base_, static_cast<double>(i)) - 1.0) / (base_ - 1.0);
            est_table_[i] = static_cast<std::uint64_t>(std::round(est));
        }
    }

    // =========================================================================
    // Core Operations
    // =========================================================================

    /// Insert an item
    // Stack-allocated array and fast RNG
    void insert(const T& key, std::uint64_t count = 1) {
        if (count == 0) return;

        // Find the columns (stack-allocated)
        constexpr std::size_t MaxDepth = 8;
        std::size_t cols[MaxDepth];
        for (std::size_t j = 0; j < depth_; ++j) {
            cols[j] = hash_for_row(j, key);
        }

        if (count <= 10) {
            // Small increments: simulate step-by-step
            for (std::uint64_t c = 0; c < count; ++c) {
                std::uint8_t min_val = 255;
                for (std::size_t j = 0; j < depth_; ++j) {
                    min_val = std::min(min_val, table_[j].get(cols[j]));
                }
                
                if (min_val >= (1 << CounterWidth) - 1) {
                    break; // Saturated
                }
                
                double prob = prob_table_[min_val];
                if (rng_.next_double() < prob) {
                    for (std::size_t j = 0; j < depth_; ++j) {
                        if (table_[j].get(cols[j]) == min_val) {
                            table_[j].set(cols[j], min_val + 1);
                        }
                    }
                }
            }
        } else {
            // Large increments: use analytical update
            std::uint8_t min_val = 255;
            for (std::size_t j = 0; j < depth_; ++j) {
                min_val = std::min(min_val, table_[j].get(cols[j]));
            }

            std::uint64_t cur_est = est_table_[min_val];
            std::uint64_t new_est = cur_est + count;
            std::uint8_t new_val = encode(new_est);

            for (std::size_t j = 0; j < depth_; ++j) {
                if (table_[j].get(cols[j]) == min_val) {
                    table_[j].set(cols[j], new_val);
                }
            }
        }
        total_count_ += count;
    }

    /// Bulk insert
    template <typename InputIt>
    void insert(InputIt first, InputIt last) {
        for (; first != last; ++first) {
            insert(*first);
        }
    }

    /// Estimate frequency
    [[nodiscard]] std::uint64_t estimate(const T& key) const noexcept {
        std::uint8_t min_val = 255;
        for (std::size_t j = 0; j < depth_; ++j) {
            std::size_t col = hash_for_row(j, key);
            min_val = std::min(min_val, table_[j].get(col));
        }
        return est_table_[min_val];
    }

    void clear() noexcept {
        for (auto& row : table_) {
            row.clear();
        }
        total_count_ = 0;
    }

    // =========================================================================
    // Statistics & Introspection
    // =========================================================================

    [[nodiscard]] std::size_t width() const noexcept { return width_; }
    [[nodiscard]] std::size_t depth() const noexcept { return depth_; }
    [[nodiscard]] std::uint64_t total_count() const noexcept { return total_count_; }
    [[nodiscard]] double base() const noexcept { return base_; }
    
    [[nodiscard]] std::size_t memory_bytes() const noexcept {
        std::size_t bytes = sizeof(CountMinLog);
        for (const auto& store : table_) {
            bytes += store.memory_bytes();
        }
        return bytes;
    }
    [[nodiscard]] std::size_t memory_usage() const noexcept { return memory_bytes(); }

    // =========================================================================
    // Serialization
    // =========================================================================

    void serialize(std::ostream& out) const {
        out.write("PCML", 4);
        std::uint8_t counter_width = CounterWidth;
        out.write(reinterpret_cast<const char*>(&counter_width), sizeof(counter_width));
        write_u64(out, width_);
        write_u64(out, depth_);
        write_double(out, base_);
        write_u64(out, total_count_);
        for (const auto& store : table_) {
            const auto& data = store.data();
            std::uint64_t data_size = data.size();
            write_u64(out, data_size);
            if (data_size > 0) {
                out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data_size));
            }
        }
        if (!out) {
            throw std::runtime_error("CountMinLog::serialize: write failed");
        }
    }

    static CountMinLog deserialize(std::istream& in, Hash hasher = Hash{}) {
        char magic[4];
        in.read(magic, 4);
        if (std::memcmp(magic, "PCML", 4) != 0) {
            throw std::runtime_error("CountMinLog::deserialize: invalid magic number");
        }

        std::uint8_t counter_width = 0;
        in.read(reinterpret_cast<char*>(&counter_width), sizeof(counter_width));
        if (counter_width != CounterWidth) {
            throw std::runtime_error("CountMinLog::deserialize: CounterWidth mismatch");
        }

        const auto width = read_u64(in);
        const auto depth = read_u64(in);
        const auto base = read_double(in);
        const auto total_count = read_u64(in);

        CountMinLog cml(width, depth, base, total_count, std::move(hasher));
        for (std::size_t j = 0; j < depth; ++j) {
            std::uint64_t data_size = read_u64(in);
            if (data_size > 0) {
                cml.table_[j].data().resize(data_size);
                in.read(reinterpret_cast<char*>(cml.table_[j].data().data()), static_cast<std::streamsize>(data_size));
            }
        }

        if (!in) {
            throw std::runtime_error("CountMinLog::deserialize: read failed");
        }
        return cml;
    }

private:
    class LogCounterStore {
    public:
        LogCounterStore() : size_(0) {}
        explicit LogCounterStore(std::size_t size) : size_(size) {
            if constexpr (CounterWidth == 4) {
                data_.assign((size + 1) / 2, 0);
            } else {
                data_.assign(size, 0);
            }
        }
        
        [[nodiscard]] std::uint8_t get(std::size_t idx) const noexcept {
            if constexpr (CounterWidth == 4) {
                std::uint8_t byte = data_[idx >> 1];
                return (idx & 1) ? (byte >> 4) : (byte & 0x0F);
            } else {
                return data_[idx];
            }
        }
        
        void set(std::size_t idx, std::uint8_t val) noexcept {
            if constexpr (CounterWidth == 4) {
                std::size_t byte_idx = idx >> 1;
                std::uint8_t mask = (idx & 1) ? 0x0F : 0xF0;
                std::uint8_t shift = (idx & 1) ? 4 : 0;
                data_[byte_idx] = (data_[byte_idx] & mask) | ((val & 0x0F) << shift);
            } else {
                data_[idx] = val;
            }
        }

        void clear() noexcept {
            std::fill(data_.begin(), data_.end(), 0);
        }

        [[nodiscard]] std::size_t size() const noexcept { return size_; }
        [[nodiscard]] std::size_t memory_bytes() const noexcept { return data_.size() * sizeof(std::uint8_t); }
        
        [[nodiscard]] const std::vector<std::uint8_t>& data() const noexcept { return data_; }
        std::vector<std::uint8_t>& data() noexcept { return data_; }
        
    private:
        std::vector<std::uint8_t> data_;
        std::size_t size_;
    };

    struct XorShift64 {
        std::uint64_t state;
        explicit XorShift64(std::uint64_t seed) : state(seed | 1) {}
        std::uint64_t next() noexcept {
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
            return state;
        }
        double next_double() noexcept {
            // Multiply by 2^-64 to get [0,1)
            return (next() >> 11) * (1.0 / (1ULL << 53));
        }
    };

    CountMinLog(std::size_t width, std::size_t depth, double base, std::uint64_t total_count, Hash hasher)
        : table_(depth),
          width_(width),
          width_mask_(width - 1),
          depth_(depth),
          total_count_(total_count),
          base_(base),
          hasher_(std::move(hasher)),
          rng_(12345ULL)
    {
        if (depth_ > 8) {
            throw std::invalid_argument("CountMinLog: depth must be <= 8");
        }
        for (std::size_t j = 0; j < depth_; ++j) {
            table_[j] = LogCounterStore(width_);
        }
        seeds_.resize(depth_);
        for (std::size_t j = 0; j < depth_; ++j) {
            seeds_[j] = fmix64(static_cast<std::uint64_t>(j + 1));
        }
        prob_table_.resize(1 << CounterWidth);
        for (size_t i = 0; i < prob_table_.size(); ++i) {
            prob_table_[i] = std::pow(base_, -static_cast<double>(i));
        }
        est_table_.resize(1 << CounterWidth);
        est_table_[0] = 0;
        for (std::size_t i = 1; i < est_table_.size(); ++i) {
            double est = (std::pow(base_, static_cast<double>(i)) - 1.0) / (base_ - 1.0);
            est_table_[i] = static_cast<std::uint64_t>(std::round(est));
        }
    }

    [[nodiscard]] std::uint8_t encode(std::uint64_t n) const noexcept {
        if (n == 0) return 0;
        auto it = std::lower_bound(est_table_.begin(), est_table_.end(), n);
        if (it == est_table_.end()) {
            return static_cast<std::uint8_t>(est_table_.size() - 1);
        }
        std::size_t idx = std::distance(est_table_.begin(), it);
        if (idx > 0 && est_table_[idx] > n) {
            // Find the closer estimate to n to simulate rounding
            if (est_table_[idx] - n > n - est_table_[idx - 1]) {
                return static_cast<std::uint8_t>(idx - 1);
            }
        }
        return static_cast<std::uint8_t>(idx);
    }

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

    static void write_u64(std::ostream& out, std::uint64_t v) {
        out.write(reinterpret_cast<const char*>(&v), sizeof(v));
    }
    static std::uint64_t read_u64(std::istream& in) {
        std::uint64_t v;
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

    std::vector<LogCounterStore> table_;
    std::vector<std::uint64_t> seeds_;
    std::size_t width_;
    std::size_t width_mask_;
    std::size_t depth_;
    std::uint64_t total_count_;
    double base_;
    Hash hasher_;
    XorShift64 rng_;
    std::vector<double> prob_table_;
    std::vector<std::uint64_t> est_table_;
};

} // namespace probds
