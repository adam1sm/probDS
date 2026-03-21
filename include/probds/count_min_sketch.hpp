#pragma once
// =============================================================================
// count_min_sketch.hpp — Count-Min Sketch
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
class CountMinSketch {
public:
    static_assert(is_hash_policy_v<Hash, T>, "Hash must satisfy the HashPolicy concept");

    // =========================================================================
    // Construction
    // =========================================================================

    explicit CountMinSketch(double epsilon = 0.01, double delta = 0.01, Hash hasher = Hash{})
        : total_count_(0), hasher_(std::move(hasher))
    {
        if (epsilon <= 0.0 || epsilon >= 1.0) {
            throw std::invalid_argument("CountMinSketch: epsilon must be in (0, 1)");
        }
        if (delta <= 0.0 || delta >= 1.0) {
            throw std::invalid_argument("CountMinSketch: delta must be in (0, 1)");
        }

        const std::size_t base_width = static_cast<std::size_t>(std::ceil(std::exp(1.0) / epsilon));
        width_ = next_power_of_two(base_width < 2 ? 2 : base_width);
        width_mask_ = width_ - 1;

        depth_ = static_cast<std::size_t>(std::ceil(std::log(1.0 / delta)));
        if (depth_ == 0) depth_ = 1;

        table_.assign(depth_, std::vector<std::uint64_t>(width_, 0));

        seeds_.resize(depth_);
        for (std::size_t j = 0; j < depth_; ++j) {
            seeds_[j] = fmix64(static_cast<std::uint64_t>(j + 1));
        }
    }

    // =========================================================================
    // Core Operations
    // =========================================================================

    /// Insert an item
    void insert(const T& key, std::uint64_t count = 1) {
        if (count == 0) return;
        for (std::size_t j = 0; j < depth_; ++j) {
            const std::size_t col = hash_for_row(j, key);
            table_[j][col] += count;
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
        std::uint64_t min_val = std::numeric_limits<std::uint64_t>::max();
        for (std::size_t j = 0; j < depth_; ++j) {
            const std::size_t col = hash_for_row(j, key);
            min_val = std::min(min_val, table_[j][col]);
        }
        return min_val;
    }

    void clear() noexcept {
        for (auto& row : table_) {
            std::fill(row.begin(), row.end(), 0);
        }
        total_count_ = 0;
    }

    // =========================================================================
    // Statistics & Introspection
    // =========================================================================

    [[nodiscard]] std::size_t width() const noexcept { return width_; }
    [[nodiscard]] std::size_t depth() const noexcept { return depth_; }
    [[nodiscard]] std::uint64_t total_count() const noexcept { return total_count_; }
    [[nodiscard]] std::size_t memory_bytes() const noexcept { return depth_ * width_ * sizeof(std::uint64_t); }
    [[nodiscard]] std::size_t memory_usage() const noexcept { return memory_bytes(); }

    // =========================================================================
    // Serialization
    // =========================================================================

    void serialize(std::ostream& out) const {
        out.write("PCM2", 4);
        write_u64(out, width_);
        write_u64(out, depth_);
        write_u64(out, total_count_);
        for (const auto& row : table_) {
            out.write(reinterpret_cast<const char*>(row.data()), static_cast<std::streamsize>(width_ * sizeof(std::uint64_t)));
        }
        if (!out) {
            throw std::runtime_error("CountMinSketch::serialize: write failed");
        }
    }

    static CountMinSketch deserialize(std::istream& in, Hash hasher = Hash{}) {
        char magic[4];
        in.read(magic, 4);
        if (std::memcmp(magic, "PCM2", 4) != 0) {
            throw std::runtime_error("CountMinSketch::deserialize: invalid magic number");
        }

        const auto width = read_u64(in);
        const auto depth = read_u64(in);
        const auto total_count = read_u64(in);

        CountMinSketch cms(width, depth, total_count, std::move(hasher));
        for (auto& row : cms.table_) {
            in.read(reinterpret_cast<char*>(row.data()), static_cast<std::streamsize>(width * sizeof(std::uint64_t)));
        }

        if (!in) {
            throw std::runtime_error("CountMinSketch::deserialize: read failed");
        }
        return cms;
    }

private:
    CountMinSketch(std::size_t width, std::size_t depth, std::uint64_t total_count, Hash hasher)
        : table_(depth, std::vector<std::uint64_t>(width, 0)),
          width_(width),
          width_mask_(width - 1),
          depth_(depth),
          total_count_(total_count),
          hasher_(std::move(hasher))
    {
        seeds_.resize(depth_);
        for (std::size_t j = 0; j < depth_; ++j) {
            seeds_[j] = fmix64(static_cast<std::uint64_t>(j + 1));
        }
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

    std::vector<std::vector<std::uint64_t>> table_;
    std::vector<std::uint64_t> seeds_;
    std::size_t width_;
    std::size_t width_mask_;
    std::size_t depth_;
    std::uint64_t total_count_;
    Hash hasher_;
};

} // namespace probds
