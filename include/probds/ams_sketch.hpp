#pragma once
// =============================================================================
// ams_sketch.hpp — Alon-Matias-Szegedy (AMS) Sketch for F2 Moment Estimation
// probDS: High-Performance Probabilistic Data Structures Library
// =============================================================================

#include "hash.hpp"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <istream>
#include <ostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace probds {

template <typename T = std::string_view, typename Hash = DefaultHash<T>>
class AMSSketch {
public:
    static_assert(is_hash_policy_v<Hash, T>, "Hash must satisfy the HashPolicy concept");

    // =========================================================================
    // Construction
    // =========================================================================

    AMSSketch(std::size_t d, std::size_t w, Hash hasher = Hash{})
        : d_(d), w_(next_pow2(w)), mask_(w_ - 1), count_(0), hasher_(std::move(hasher)),
          counters_(d_, std::vector<std::int64_t>(w_, 0))
    {
        if (d == 0) {
            throw std::invalid_argument("AMSSketch: depth d must be > 0");
        }
        if (w == 0) {
            throw std::invalid_argument("AMSSketch: width w must be > 0");
        }
    }

    // =========================================================================
    // Core Operations
    // =========================================================================

    /// Insert an item with an optional weight/count
    void insert(const T& key, std::int64_t count = 1) {
        if (count == 0) return;
        const std::uint64_t hash_val = hasher_(key);

        for (std::size_t i = 0; i < d_; ++i) {
            // Generate independent hash for row i
            const std::uint64_t row_hash = fmix64(hash_val + i * 0x9e3779b97f4a7c15ULL);
            
            // Map to column using bitmask (since w_ is power of 2)
            const std::size_t col = row_hash & mask_;
            
            // Generate sign: +1 if MSB is 1, -1 if MSB is 0
            const std::int64_t sign = ((row_hash >> 63) & 1ULL) ? 1 : -1;
            
            counters_[i][col] += count * sign;
        }
        count_ += std::abs(count);
    }

    /// Estimate the second frequency moment (F2)
    [[nodiscard]] double estimate_f2() const {
        if (d_ == 0 || w_ == 0) return 0.0;
        
        std::vector<double> row_estimates(d_);
        for (std::size_t i = 0; i < d_; ++i) {
            double sum_sq = 0.0;
            for (std::size_t j = 0; j < w_; ++j) {
                double val = static_cast<double>(counters_[i][j]);
                sum_sq += val * val;
            }
            row_estimates[i] = sum_sq;
        }

        std::sort(row_estimates.begin(), row_estimates.end());
        
        // Return median of estimates
        if (d_ % 2 == 1) {
            return row_estimates[d_ / 2];
        } else {
            return (row_estimates[d_ / 2 - 1] + row_estimates[d_ / 2]) / 2.0;
        }
    }

    /// Merge another AMSSketch
    void merge(const AMSSketch& other) {
        if (d_ != other.d_ || w_ != other.w_) {
            throw std::invalid_argument("AMSSketch::merge: incompatible sketch dimensions");
        }
        for (std::size_t i = 0; i < d_; ++i) {
            for (std::size_t j = 0; j < w_; ++j) {
                counters_[i][j] += other.counters_[i][j];
            }
        }
        count_ += other.count_;
    }

    // =========================================================================
    // Serialization
    // =========================================================================

    void serialize(std::ostream& out) const {
        out.write("AMS1", 4);
        write_u64(out, d_);
        write_u64(out, w_);
        write_u64(out, count_);
        
        for (std::size_t i = 0; i < d_; ++i) {
            out.write(reinterpret_cast<const char*>(counters_[i].data()),
                      static_cast<std::streamsize>(w_ * sizeof(std::int64_t)));
        }
        if (!out) {
            throw std::runtime_error("AMSSketch::serialize: write failed");
        }
    }

    static AMSSketch deserialize(std::istream& in, Hash hasher = Hash{}) {
        char magic[4];
        in.read(magic, 4);
        if (std::memcmp(magic, "AMS1", 4) != 0) {
            throw std::runtime_error("AMSSketch::deserialize: invalid magic number");
        }

        const auto d = read_u64(in);
        const auto w = read_u64(in);
        const auto count = read_u64(in);

        AMSSketch sketch(d, w, std::move(hasher));
        sketch.count_ = count;

        for (std::size_t i = 0; i < d; ++i) {
            in.read(reinterpret_cast<char*>(sketch.counters_[i].data()),
                    static_cast<std::streamsize>(w * sizeof(std::int64_t)));
        }

        if (!in) {
            throw std::runtime_error("AMSSketch::deserialize: read failed");
        }

        return sketch;
    }

    // =========================================================================
    // Statistics & Introspection
    // =========================================================================

    [[nodiscard]] std::size_t depth() const noexcept { return d_; }
    [[nodiscard]] std::size_t width() const noexcept { return w_; }
    [[nodiscard]] std::size_t size() const noexcept { return count_; }
    [[nodiscard]] std::size_t memory_bytes() const noexcept {
        return sizeof(AMSSketch) + d_ * sizeof(std::vector<std::int64_t>) + d_ * w_ * sizeof(std::int64_t);
    }

    const std::vector<std::vector<std::int64_t>>& counters() const noexcept { return counters_; }

    void clear() noexcept {
        for (auto& row : counters_) {
            std::fill(row.begin(), row.end(), 0);
        }
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

    static std::size_t next_pow2(std::size_t n) noexcept {
        if (n == 0) return 1;
        n--;
        n |= n >> 1;
        n |= n >> 2;
        n |= n >> 4;
        n |= n >> 8;
        n |= n >> 16;
        n |= n >> 32;
        n++;
        return n;
    }

    static void write_u64(std::ostream& out, std::uint64_t v) {
        out.write(reinterpret_cast<const char*>(&v), sizeof(v));
    }

    static std::uint64_t read_u64(std::istream& in) {
        std::uint64_t v;
        in.read(reinterpret_cast<char*>(&v), sizeof(v));
        return v;
    }

    std::size_t d_;
    std::size_t w_;
    std::size_t mask_;
    std::size_t count_;
    Hash hasher_;
    std::vector<std::vector<std::int64_t>> counters_;
};

} // namespace probds
