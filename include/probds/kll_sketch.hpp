#pragma once
// =============================================================================
// kll_sketch.hpp — KLL Sketch for Space-Optimal Quantile Estimation
// probDS: High-Performance Probabilistic Data Structures Library
// =============================================================================

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <istream>
#include <limits>
#include <ostream>
#include <random>
#include <stdexcept>
#include <utility>
#include <vector>

namespace probds {

class KLLSketch {
public:
    struct WeightedItem {
        double value;
        std::uint64_t weight;

        bool operator<(const WeightedItem& other) const noexcept {
            return value < other.value;
        }
    };

    // =========================================================================
    // Construction
    // =========================================================================

    explicit KLLSketch(std::size_t k = 200, std::uint64_t seed = 12345ULL)
        : k_(k),
          n_(0),
          min_val_(std::numeric_limits<double>::max()),
          max_val_(-std::numeric_limits<double>::max()),
          rng_(seed),
          levels_(1),
          compaction_count_(0)
    {
        if (k < 8) {
            throw std::invalid_argument("KLLSketch: k must be >= 8");
        }
    }

    // =========================================================================
    // Core Operations
    // =========================================================================

    /// Insert a single value
    void insert(double value) {
        cache_valid_ = false;
        levels_[0].push_back(value);
        ++n_;
        min_val_ = std::min(min_val_, value);
        max_val_ = std::max(max_val_, value);

        if (levels_[0].size() > get_capacity(0)) {
            compact();
        }
    }

    /// Merge another KLL sketch into this one
    void merge(const KLLSketch& other) {
        if (other.n_ == 0) return;
        cache_valid_ = false;

        while (levels_.size() < other.levels_.size()) {
            levels_.emplace_back();
        }
        for (std::size_t i = 0; i < other.levels_.size(); ++i) {
            levels_[i].insert(levels_[i].end(), other.levels_[i].begin(), other.levels_[i].end());
        }
        n_ += other.n_;
        min_val_ = std::min(min_val_, other.min_val_);
        max_val_ = std::max(max_val_, other.max_val_);

        compact();
    }

    /// Estimate value at quantile q in [0, 1]
    [[nodiscard]] double get_quantile(double q) const {
        if (n_ == 0) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        if (q <= 0.0) return min_val_;
        if (q >= 1.0) return max_val_;

        if (!cache_valid_) {
            sorted_cache_.clear();
            std::size_t num_items = 0;
            for (const auto& level : levels_) {
                num_items += level.size();
            }
            sorted_cache_.reserve(num_items);

            for (std::size_t i = 0; i < levels_.size(); ++i) {
                const std::uint64_t weight = 1ULL << i;
                for (double val : levels_[i]) {
                    sorted_cache_.push_back({val, weight});
                }
            }

            if (sorted_cache_.empty()) {
                return std::numeric_limits<double>::quiet_NaN();
            }

            std::sort(sorted_cache_.begin(), sorted_cache_.end());
            cache_valid_ = true;
        }

        double target = q * static_cast<double>(n_);
        double sum = 0.0;
        for (const auto& item : sorted_cache_) {
            sum += static_cast<double>(item.weight);
            if (sum >= target) {
                return item.value;
            }
        }
        return sorted_cache_.back().value;
    }

    // =========================================================================
    // Serialization
    // =========================================================================

    void serialize(std::ostream& out) const {
        out.write("PKL1", 4);
        write_u64(out, k_);
        write_u64(out, n_);
        out.write(reinterpret_cast<const char*>(&min_val_), sizeof(min_val_));
        out.write(reinterpret_cast<const char*>(&max_val_), sizeof(max_val_));
        
        // Serialize RNG state
        out << rng_ << "\n";

        write_u64(out, levels_.size());
        for (const auto& level : levels_) {
            write_u64(out, level.size());
            out.write(reinterpret_cast<const char*>(level.data()),
                      static_cast<std::streamsize>(level.size() * sizeof(double)));
        }
        if (!out) {
            throw std::runtime_error("KLLSketch::serialize: write failed");
        }
    }

    static KLLSketch deserialize(std::istream& in) {
        char magic[4];
        in.read(magic, 4);
        if (std::memcmp(magic, "PKL1", 4) != 0) {
            throw std::runtime_error("KLLSketch::deserialize: invalid magic number");
        }

        const auto k = read_u64(in);
        const auto n = read_u64(in);
        double min_val, max_val;
        in.read(reinterpret_cast<char*>(&min_val), sizeof(min_val));
        in.read(reinterpret_cast<char*>(&max_val), sizeof(max_val));
        
        KLLSketch kll(k);
        kll.n_ = n;
        kll.min_val_ = min_val;
        kll.max_val_ = max_val;

        // Deserialize RNG state
        in >> kll.rng_;
        char newline;
        in.get(newline); // Consume trailing newline

        const auto num_levels = read_u64(in);
        kll.levels_.resize(num_levels);

        for (std::size_t i = 0; i < num_levels; ++i) {
            const auto level_size = read_u64(in);
            kll.levels_[i].resize(level_size);
            in.read(reinterpret_cast<char*>(kll.levels_[i].data()),
                    static_cast<std::streamsize>(level_size * sizeof(double)));
        }

        if (!in) {
            throw std::runtime_error("KLLSketch::deserialize: read failed");
        }

        return kll;
    }

    // =========================================================================
    // Statistics & Introspection
    // =========================================================================

    [[nodiscard]] std::size_t k() const noexcept { return k_; }
    [[nodiscard]] std::uint64_t size() const noexcept { return n_; }
    [[nodiscard]] double min() const noexcept { return min_val_; }
    [[nodiscard]] double max() const noexcept { return max_val_; }
    
    [[nodiscard]] std::size_t memory_bytes() const noexcept {
        std::size_t total = sizeof(KLLSketch) + levels_.capacity() * sizeof(std::vector<double>);
        for (const auto& lvl : levels_) {
            total += lvl.capacity() * sizeof(double);
        }
        return total;
    }

    [[nodiscard]] std::uint64_t compaction_count() const noexcept { return compaction_count_; }
    void reset_compaction_count() noexcept { compaction_count_ = 0; }

    void clear() noexcept {
        levels_.clear();
        levels_.emplace_back();
        n_ = 0;
        min_val_ = std::numeric_limits<double>::max();
        max_val_ = -std::numeric_limits<double>::max();
        cache_valid_ = false;
        sorted_cache_.clear();
        compaction_count_ = 0;
    }

private:
    [[nodiscard]] std::size_t get_capacity(std::size_t i) const noexcept {
        std::size_t h = levels_.size();
        if (i >= h) return k_;
        double cap = static_cast<double>(k_) * std::pow(2.0 / 3.0, static_cast<double>(h - 1 - i));
        std::size_t c = static_cast<std::size_t>(std::round(cap));
        return c < 2 ? 2 : c;
    }

    bool next_bool() noexcept {
        std::uniform_int_distribution<int> dist(0, 1);
        return dist(rng_) == 1;
    }

    void compact() {
        bool changed = true;
        while (changed) {
            changed = false;
            for (std::size_t i = 0; i < levels_.size(); ++i) {
                if (levels_[i].size() > get_capacity(i)) {
                    compaction_count_++;
                    cache_valid_ = false;
                    if (i + 1 == levels_.size()) {
                        levels_.emplace_back();
                    }
                    std::sort(levels_[i].begin(), levels_[i].end());

                    std::size_t size = levels_[i].size();
                    std::size_t to_compact = size;
                    std::size_t start_idx = 0;

                    // If size is odd, leave 1 element to keep compaction even
                    if (size % 2 == 1) {
                        to_compact = size - 1;
                        if (next_bool()) {
                            start_idx = 1;
                        }
                    }

                    // Promote exactly 1 of each pair randomly
                    for (std::size_t j = 0; j < to_compact; j += 2) {
                        bool pick_second = next_bool();
                        levels_[i + 1].push_back(levels_[i][start_idx + j + (pick_second ? 1 : 0)]);
                    }

                    // Clear compacted elements, keeping the one left-out element if odd
                    if (size % 2 == 1) {
                        double kept = levels_[i][start_idx == 1 ? 0 : size - 1];
                        levels_[i].clear();
                        levels_[i].push_back(kept);
                    } else {
                        levels_[i].clear();
                    }

                    changed = true;
                }
            }
        }
    }

    static void write_u64(std::ostream& out, std::uint64_t v) {
        out.write(reinterpret_cast<const char*>(&v), sizeof(v));
    }

    static std::uint64_t read_u64(std::istream& in) {
        std::uint64_t v;
        in.read(reinterpret_cast<char*>(&v), sizeof(v));
        return v;
    }

    std::size_t k_;
    std::uint64_t n_;
    double min_val_;
    double max_val_;
    std::mt19937_64 rng_;
    std::vector<std::vector<double>> levels_;
    mutable std::vector<WeightedItem> sorted_cache_;
    mutable bool cache_valid_ = false;
    std::uint64_t compaction_count_ = 0;
};

} // namespace probds
