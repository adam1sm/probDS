#pragma once
// =============================================================================
// scalable_bloom_filter.hpp — Scalable Bloom Filter
// probDS: High-Performance Probabilistic Data Structures Library
// =============================================================================

#include "hash.hpp"
#include "bloom_filter.hpp"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <istream>
#include <ostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace probds {

template <typename T = std::string_view, typename Hash = DefaultHash<T>>
class ScalableBloomFilter {
public:
    static_assert(is_hash_policy_v<Hash, T>, "Hash must satisfy the HashPolicy concept");

    // =========================================================================
    // Construction
    // =========================================================================

    explicit ScalableBloomFilter(std::size_t initial_capacity,
                                 double false_positive_rate = 0.01,
                                 double growth_factor = 2.0,
                                 double tightening_ratio = 0.8,
                                 Hash hasher = Hash{})
        : initial_capacity_(initial_capacity),
          fp_rate_(false_positive_rate),
          growth_factor_(growth_factor),
          tightening_ratio_(tightening_ratio),
          total_count_(0),
          hasher_(std::move(hasher))
    {
        if (initial_capacity == 0) {
            throw std::invalid_argument("ScalableBloomFilter: initial_capacity must be > 0");
        }
        if (false_positive_rate <= 0.0 || false_positive_rate >= 1.0) {
            throw std::invalid_argument("ScalableBloomFilter: false_positive_rate must be in (0, 1)");
        }
        if (growth_factor <= 1.0) {
            throw std::invalid_argument("ScalableBloomFilter: growth_factor must be > 1.0");
        }
        if (tightening_ratio <= 0.0 || tightening_ratio >= 1.0) {
            throw std::invalid_argument("ScalableBloomFilter: tightening_ratio must be in (0, 1)");
        }

        // Bounded target FPR: p0 = P * (1 - r)
        const double p0 = fp_rate_ * (1.0 - tightening_ratio_);
        stages_.emplace_back(initial_capacity_, p0, hasher_);
    }

    // =========================================================================
    // Core Operations
    // =========================================================================

    /// Insert a single key
    void insert(const T& key) {
        // If the current active stage has reached capacity, add a new stage
        const std::size_t active_idx = stages_.size() - 1;
        const std::size_t active_capacity = static_cast<std::size_t>(
            static_cast<double>(initial_capacity_) * std::pow(growth_factor_, active_idx));

        if (stages_.back().size() >= active_capacity) {
            const double next_fpr = fp_rate_ * (1.0 - tightening_ratio_) * std::pow(tightening_ratio_, stages_.size());
            const std::size_t next_capacity = static_cast<std::size_t>(
                static_cast<double>(initial_capacity_) * std::pow(growth_factor_, stages_.size()));
            stages_.emplace_back(next_capacity, next_fpr, hasher_);
        }

        stages_.back().insert(key);
        ++total_count_;
    }

    /// Bulk insert
    template <typename InputIt>
    void insert(InputIt first, InputIt last) {
        for (; first != last; ++first) {
            insert(*first);
        }
    }

    /// Query set membership
    [[nodiscard]] bool possibly_contains(const T& key) const noexcept {
        if (stages_.empty()) return false;
        const auto [h1, h2] = stages_[0].get_hash_pair(key);
        // Search in reverse order since the latest stages contain the most elements
        for (auto it = stages_.rbegin(); it != stages_.rend(); ++it) {
            if (it->possibly_contains(h1, h2)) {
                return true;
            }
        }
        return false;
    }

    void clear() noexcept {
        stages_.clear();
        total_count_ = 0;
        const double p0 = fp_rate_ * (1.0 - tightening_ratio_);
        stages_.emplace_back(initial_capacity_, p0, hasher_);
    }

    // =========================================================================
    // Set Operations
    // =========================================================================

    ScalableBloomFilter& operator|=(const ScalableBloomFilter& other) {
        check_compatible(other, "operator|=");
        for (std::size_t i = 0; i < stages_.size(); ++i) {
            stages_[i] |= other.stages_[i];
        }
        total_count_ += other.total_count_;
        return *this;
    }

    ScalableBloomFilter& operator&=(const ScalableBloomFilter& other) {
        check_compatible(other, "operator&=");
        for (std::size_t i = 0; i < stages_.size(); ++i) {
            stages_[i] &= other.stages_[i];
        }
        total_count_ = std::min(total_count_, other.total_count_);
        return *this;
    }

    friend ScalableBloomFilter operator|(ScalableBloomFilter lhs, const ScalableBloomFilter& rhs) {
        lhs |= rhs;
        return lhs;
    }

    friend ScalableBloomFilter operator&(ScalableBloomFilter lhs, const ScalableBloomFilter& rhs) {
        lhs &= rhs;
        return lhs;
    }

    // =========================================================================
    // Statistics & Introspection
    // =========================================================================

    [[nodiscard]] std::size_t size() const noexcept { return total_count_; }
    [[nodiscard]] std::size_t stage_count() const noexcept { return stages_.size(); }

    [[nodiscard]] std::size_t memory_bytes() const noexcept {
        std::size_t bytes = 0;
        for (const auto& stage : stages_) {
            bytes += stage.memory_bytes();
        }
        return bytes;
    }
    [[nodiscard]] std::size_t memory_usage() const noexcept { return memory_bytes(); }

    [[nodiscard]] double fill_ratio() const noexcept {
        // Average fill ratio across all stages
        double sum = 0.0;
        for (const auto& stage : stages_) {
            sum += stage.fill_ratio();
        }
        return sum / static_cast<double>(stages_.size());
    }

    [[nodiscard]] double expected_fpr() const noexcept {
        // FPR = 1 - prod(1 - p_i)
        double prod = 1.0;
        for (const auto& stage : stages_) {
            prod *= (1.0 - stage.expected_fpr());
        }
        return 1.0 - prod;
    }

    // =========================================================================
    // Serialization
    // =========================================================================

    void serialize(std::ostream& out) const {
        out.write("PSB2", 4);
        write_u64(out, initial_capacity_);
        write_double(out, fp_rate_);
        write_double(out, growth_factor_);
        write_double(out, tightening_ratio_);
        write_u64(out, total_count_);
        write_u64(out, stages_.size());
        for (const auto& stage : stages_) {
            stage.serialize(out);
        }
        if (!out) {
            throw std::runtime_error("ScalableBloomFilter::serialize: write failed");
        }
    }

    static ScalableBloomFilter deserialize(std::istream& in, Hash hasher = Hash{}) {
        char magic[4];
        in.read(magic, 4);
        if (std::memcmp(magic, "PSB2", 4) != 0) {
            throw std::runtime_error("ScalableBloomFilter::deserialize: invalid magic number");
        }

        const auto initial_capacity = read_u64(in);
        const auto fp_rate = read_double(in);
        const auto growth_factor = read_double(in);
        const auto tightening_ratio = read_double(in);
        const auto total_count = read_u64(in);
        const auto num_stages = read_u64(in);

        ScalableBloomFilter filter(initial_capacity, fp_rate, growth_factor, tightening_ratio, total_count, std::move(hasher));
        filter.stages_.clear(); // Remove default initial stage

        for (std::size_t i = 0; i < num_stages; ++i) {
            filter.stages_.push_back(BloomFilter<T, Hash>::deserialize(in, filter.hasher_));
        }

        if (!in) {
            throw std::runtime_error("ScalableBloomFilter::deserialize: read failed");
        }

        return filter;
    }

private:
    ScalableBloomFilter(std::size_t initial_capacity, double fp_rate, double growth_factor,
                        double tightening_ratio, std::size_t total_count, Hash hasher)
        : initial_capacity_(initial_capacity),
          fp_rate_(fp_rate),
          growth_factor_(growth_factor),
          tightening_ratio_(tightening_ratio),
          total_count_(total_count),
          hasher_(std::move(hasher))
    {}

    void check_compatible(const ScalableBloomFilter& other, const char* op) const {
        if (initial_capacity_ != other.initial_capacity_ ||
            fp_rate_ != other.fp_rate_ ||
            growth_factor_ != other.growth_factor_ ||
            tightening_ratio_ != other.tightening_ratio_ ||
            stages_.size() != other.stages_.size()) {
            throw std::invalid_argument(
                std::string("ScalableBloomFilter::") + op + ": filters are incompatible");
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
    static void write_double(std::ostream& out, double v) {
        out.write(reinterpret_cast<const char*>(&v), sizeof(v));
    }
    static double read_double(std::istream& in) {
        double v;
        in.read(reinterpret_cast<char*>(&v), sizeof(v));
        return v;
    }

    std::size_t initial_capacity_;
    double fp_rate_;
    double growth_factor_;
    double tightening_ratio_;
    std::size_t total_count_;
    Hash hasher_;
    std::vector<BloomFilter<T, Hash>> stages_;
};

} // namespace probds
