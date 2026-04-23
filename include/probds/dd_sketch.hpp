#pragma once
// =============================================================================
// dd_sketch.hpp — DDSketch for Relative-Error Quantile Estimation
// probDS: High-Performance Probabilistic Data Structures Library
// =============================================================================

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

class DDSketch {
public:
    struct Store {
        std::vector<std::uint64_t> counts;
        std::int32_t offset = 0;

        void add(std::int32_t idx) {
            if (counts.empty()) {
                offset = idx;
                counts.push_back(1);
                return;
            }

            std::int32_t min_idx = offset;
            std::int32_t max_idx = offset + static_cast<std::int32_t>(counts.size()) - 1;

            if (idx < min_idx) {
                std::size_t new_size = static_cast<std::size_t>(max_idx - idx + 1);
                std::vector<std::uint64_t> new_counts(new_size, 0);
                std::copy(counts.begin(), counts.end(), new_counts.begin() + (min_idx - idx));
                counts = std::move(new_counts);
                offset = idx;
            } else if (idx > max_idx) {
                std::size_t new_size = static_cast<std::size_t>(idx - min_idx + 1);
                counts.resize(new_size, 0);
            }

            counts[static_cast<std::size_t>(idx - offset)]++;
        }

        void merge(const Store& other) {
            if (other.counts.empty()) return;
            if (counts.empty()) {
                counts = other.counts;
                offset = other.offset;
                return;
            }

            std::int32_t this_min = offset;
            std::int32_t this_max = offset + static_cast<std::int32_t>(counts.size()) - 1;
            std::int32_t other_min = other.offset;
            std::int32_t other_max = other.offset + static_cast<std::int32_t>(other.counts.size()) - 1;

            std::int32_t new_min = std::min(this_min, other_min);
            std::int32_t new_max = std::max(this_max, other_max);

            std::size_t new_size = static_cast<std::size_t>(new_max - new_min + 1);
            std::vector<std::uint64_t> new_counts(new_size, 0);

            std::copy(counts.begin(), counts.end(), new_counts.begin() + (this_min - new_min));

            for (std::size_t i = 0; i < other.counts.size(); ++i) {
                std::size_t target_idx = static_cast<std::size_t>((other.offset + i) - new_min);
                new_counts[target_idx] += other.counts[i];
            }

            counts = std::move(new_counts);
            offset = new_min;
        }

        void serialize(std::ostream& out) const {
            out.write(reinterpret_cast<const char*>(&offset), sizeof(offset));
            std::uint64_t size = counts.size();
            out.write(reinterpret_cast<const char*>(&size), sizeof(size));
            if (size > 0) {
                out.write(reinterpret_cast<const char*>(counts.data()), static_cast<std::streamsize>(size * sizeof(std::uint64_t)));
            }
        }

        void deserialize(std::istream& in) {
            in.read(reinterpret_cast<char*>(&offset), sizeof(offset));
            std::uint64_t size;
            in.read(reinterpret_cast<char*>(&size), sizeof(size));
            counts.resize(size);
            if (size > 0) {
                in.read(reinterpret_cast<char*>(counts.data()), static_cast<std::streamsize>(size * sizeof(std::uint64_t)));
            }
        }

        void clear() noexcept {
            counts.clear();
            offset = 0;
        }
    };

    // =========================================================================
    // Construction & Destruction
    // =========================================================================

    explicit DDSketch(double alpha = 0.01)
        : alpha_(alpha), zero_count_(0), total_count_(0)
    {
        if (alpha <= 0.0 || alpha >= 1.0) {
            throw std::invalid_argument("DDSketch: alpha must be in (0, 1)");
        }
        gamma_ = (1.0 + alpha_) / (1.0 - alpha_);
        ln_gamma_ = std::log(gamma_);
    }

    ~DDSketch() = default;

    DDSketch(const DDSketch&) = default;
    DDSketch& operator=(const DDSketch&) = default;
    DDSketch(DDSketch&&) noexcept = default;
    DDSketch& operator=(DDSketch&&) noexcept = default;

    // =========================================================================
    // Core Operations
    // =========================================================================

    /// Insert a single value
    void insert(double value) {
        if (value == 0.0) {
            zero_count_++;
        } else if (value > 0.0) {
            std::int32_t idx = get_index(value);
            positive_store_.add(idx);
        } else {
            std::int32_t idx = get_index(-value); // absolute value
            negative_store_.add(idx);
        }
        total_count_++;
    }

    /// Merge another DDSketch into this one
    void merge(const DDSketch& other) {
        // Tolerant comparison of floating point alpha targets
        if (std::abs(alpha_ - other.alpha_) > 1e-9) {
            throw std::invalid_argument("DDSketch: cannot merge sketches with different alpha values");
        }
        positive_store_.merge(other.positive_store_);
        negative_store_.merge(other.negative_store_);
        zero_count_ += other.zero_count_;
        total_count_ += other.total_count_;
    }

    /// Estimate value at quantile q in [0, 1]
    [[nodiscard]] double get_quantile(double q) const {
        if (total_count_ == 0) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        if (q < 0.0) q = 0.0;
        if (q > 1.0) q = 1.0;

        double target = q * static_cast<double>(total_count_);
        double accumulated = 0.0;

        // 1. Iterate through negative store (in reverse index order: largest absolute to smallest)
        if (!negative_store_.counts.empty()) {
            for (std::int32_t i = static_cast<std::int32_t>(negative_store_.counts.size()) - 1; i >= 0; --i) {
                std::uint64_t count = negative_store_.counts[i];
                if (count > 0) {
                    accumulated += static_cast<double>(count);
                    if (accumulated >= target) {
                        std::int32_t idx = i + negative_store_.offset;
                        return -get_representative_value(idx);
                    }
                }
            }
        }

        // 2. Iterate through zero_count
        if (zero_count_ > 0) {
            accumulated += static_cast<double>(zero_count_);
            if (accumulated >= target) {
                return 0.0;
            }
        }

        // 3. Iterate through positive store (in forward index order: smallest index to largest)
        if (!positive_store_.counts.empty()) {
            for (std::size_t i = 0; i < positive_store_.counts.size(); ++i) {
                std::uint64_t count = positive_store_.counts[i];
                if (count > 0) {
                    accumulated += static_cast<double>(count);
                    if (accumulated >= target) {
                        std::int32_t idx = static_cast<std::int32_t>(i) + positive_store_.offset;
                        return get_representative_value(idx);
                    }
                }
            }
        }

        // Fallback to highest index in positive store
        if (!positive_store_.counts.empty()) {
            std::int32_t idx = static_cast<std::int32_t>(positive_store_.counts.size()) - 1 + positive_store_.offset;
            return get_representative_value(idx);
        }
        return 0.0;
    }

    // =========================================================================
    // Introspection
    // =========================================================================

    [[nodiscard]] double alpha() const noexcept { return alpha_; }
    [[nodiscard]] std::uint64_t size() const noexcept { return total_count_; }
    [[nodiscard]] std::uint64_t zero_count() const noexcept { return zero_count_; }

    [[nodiscard]] std::size_t memory_bytes() const noexcept {
        return sizeof(DDSketch) + 
               positive_store_.counts.capacity() * sizeof(std::uint64_t) +
               negative_store_.counts.capacity() * sizeof(std::uint64_t);
    }
    [[nodiscard]] std::size_t memory_usage() const noexcept { return memory_bytes(); }

    void clear() noexcept {
        positive_store_.clear();
        negative_store_.clear();
        zero_count_ = 0;
        total_count_ = 0;
    }

    // =========================================================================
    // Serialization
    // =========================================================================

    void serialize(std::ostream& out) const {
        out.write("PDS1", 4);
        out.write(reinterpret_cast<const char*>(&alpha_), sizeof(alpha_));
        out.write(reinterpret_cast<const char*>(&zero_count_), sizeof(zero_count_));
        out.write(reinterpret_cast<const char*>(&total_count_), sizeof(total_count_));
        positive_store_.serialize(out);
        negative_store_.serialize(out);
        if (!out) {
            throw std::runtime_error("DDSketch::serialize: write failed");
        }
    }

    static DDSketch deserialize(std::istream& in) {
        char magic[4];
        in.read(magic, 4);
        if (std::memcmp(magic, "PDS1", 4) != 0) {
            throw std::runtime_error("DDSketch::deserialize: invalid magic number");
        }

        double alpha;
        in.read(reinterpret_cast<char*>(&alpha), sizeof(alpha));

        DDSketch sketch(alpha);
        in.read(reinterpret_cast<char*>(&sketch.zero_count_), sizeof(sketch.zero_count_));
        in.read(reinterpret_cast<char*>(&sketch.total_count_), sizeof(sketch.total_count_));
        sketch.positive_store_.deserialize(in);
        sketch.negative_store_.deserialize(in);

        if (!in) {
            throw std::runtime_error("DDSketch::deserialize: read failed");
        }
        return sketch;
    }

private:
    [[nodiscard]] std::int32_t get_index(double value) const noexcept {
        return static_cast<std::int32_t>(std::ceil(std::log(value) / ln_gamma_));
    }

    [[nodiscard]] double get_representative_value(std::int32_t index) const noexcept {
        return (2.0 * std::pow(gamma_, index)) / (gamma_ + 1.0);
    }

    double alpha_;
    double gamma_;
    double ln_gamma_;
    std::uint64_t zero_count_;
    std::uint64_t total_count_;
    Store positive_store_;
    Store negative_store_;
};

} // namespace probds
