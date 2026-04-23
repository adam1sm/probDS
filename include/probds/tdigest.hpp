#pragma once
// =============================================================================
// tdigest.hpp — t-Digest Sketch for Quantile Estimation
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

class tDigest {
public:
    struct Centroid {
        double mean;
        double weight;
    };

    // =========================================================================
    // Construction
    // =========================================================================

    explicit tDigest(double compression = 100.0)
        : compression_(compression),
          total_weight_(0.0),
          min_val_(std::numeric_limits<double>::max()),
          max_val_(-std::numeric_limits<double>::max()),
          dirty_(false)
    {
        if (compression <= 0.0) {
            throw std::invalid_argument("tDigest: compression must be > 0");
        }
    }

    // =========================================================================
    // Core Operations
    // =========================================================================

    /// Insert a single value with an optional weight
    void insert(double value, double weight = 1.0) {
        if (weight <= 0.0) {
            throw std::invalid_argument("tDigest::insert: weight must be > 0");
        }
        centroids_.push_back(Centroid{value, weight});
        total_weight_ += weight;
        min_val_ = std::min(min_val_, value);
        max_val_ = std::max(max_val_, value);
        dirty_ = true;

        if (centroids_.size() > 30 * compression_) {
            compress();
        }
    }

    /// Merge another t-Digest sketch into this one
    void merge(const tDigest& other) {
        if (other.total_weight_ == 0.0) return;
        
        other.compress();
        centroids_.reserve(centroids_.size() + other.centroids_.size());
        for (const auto& c : other.centroids_) {
            centroids_.push_back(c);
        }
        total_weight_ += other.total_weight_;
        min_val_ = std::min(min_val_, other.min_val_);
        max_val_ = std::max(max_val_, other.max_val_);
        dirty_ = true;

        compress();
    }

    /// Force compression/compaction of centroids
    void compress() const {
        if (!dirty_ || centroids_.size() <= 1) {
            dirty_ = false;
            return;
        }

        std::sort(centroids_.begin(), centroids_.end(), [](const Centroid& a, const Centroid& b) {
            return a.mean < b.mean;
        });

        std::vector<Centroid> merged;
        merged.reserve(centroids_.size());
        merged.push_back(centroids_[0]);

        double w_sum = centroids_[0].weight;
        for (std::size_t i = 1; i < centroids_.size(); ++i) {
            const auto& c = centroids_[i];
            // Cumulative weight fraction at the center of the merged group
            double q = (w_sum + c.weight / 2.0) / total_weight_;
            // Scale constraint: max weight limit
            double limit = total_weight_ * 4.0 * q * (1.0 - q) / compression_;
            if (limit < 1.0) limit = 1.0;

            if (merged.back().weight + c.weight <= limit) {
                // Merge into current group
                double new_weight = merged.back().weight + c.weight;
                merged.back().mean = (merged.back().mean * merged.back().weight + c.mean * c.weight) / new_weight;
                merged.back().weight = new_weight;
            } else {
                // Start a new group
                merged.push_back(c);
            }
            w_sum += c.weight;
        }

        centroids_ = std::move(merged);
        dirty_ = false;
    }

    /// Estimate value at quantile q in [0, 1]
    [[nodiscard]] double quantile(double q) const {
        if (total_weight_ == 0.0) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        if (q <= 0.0) return min_val_;
        if (q >= 1.0) return max_val_;

        compress();

        if (centroids_.size() == 1) {
            return centroids_[0].mean;
        }

        double target_weight = q * total_weight_;
        double cur_weight = 0.0;

        for (std::size_t i = 0; i < centroids_.size(); ++i) {
            double next_weight = cur_weight + centroids_[i].weight;
            if (target_weight <= next_weight) {
                // Interpolate
                double prev_center = 0.0;
                double prev_mean = min_val_;
                if (i > 0) {
                    prev_center = cur_weight - centroids_[i - 1].weight / 2.0;
                    prev_mean = centroids_[i - 1].mean;
                }

                double curr_center = cur_weight + centroids_[i].weight / 2.0;
                double curr_mean = centroids_[i].mean;

                if (target_weight <= curr_center) {
                    if (i == 0) {
                        double p2 = centroids_[0].weight / 2.0;
                        double t = target_weight / p2;
                        return min_val_ + t * (centroids_[0].mean - min_val_);
                    }
                    double t = (target_weight - prev_center) / (curr_center - prev_center);
                    return prev_mean + t * (curr_mean - prev_mean);
                } else {
                    double next_center = total_weight_;
                    double next_mean = max_val_;
                    if (i + 1 < centroids_.size()) {
                        next_center = cur_weight + centroids_[i].weight + centroids_[i + 1].weight / 2.0;
                        next_mean = centroids_[i + 1].mean;
                    }
                    double t = (target_weight - curr_center) / (next_center - curr_center);
                    return curr_mean + t * (next_mean - curr_mean);
                }
            }
            cur_weight = next_weight;
        }
        return max_val_;
    }

    /// Estimate cumulative distribution function at x
    [[nodiscard]] double cdf(double x) const {
        if (total_weight_ == 0.0) {
            return std::numeric_limits<double>::quiet_NaN();
        }

        compress();

        if (centroids_.size() == 1) {
            if (x < centroids_[0].mean) return 0.0;
            if (x > centroids_[0].mean) return 1.0;
            return 0.5;
        }

        if (x < min_val_) return 0.0;
        if (x >= max_val_) return 1.0;

        double cur_weight = 0.0;
        for (std::size_t i = 0; i < centroids_.size(); ++i) {
            if (x < centroids_[i].mean) {
                if (i == 0) {
                    double p = (x - min_val_) / (centroids_[0].mean - min_val_);
                    double w = p * (centroids_[0].weight / 2.0);
                    return w / total_weight_;
                } else {
                    double prev_center = cur_weight - centroids_[i - 1].weight / 2.0;
                    double curr_center = cur_weight + centroids_[i].weight / 2.0;
                    double p = (x - centroids_[i - 1].mean) / (centroids_[i].mean - centroids_[i - 1].mean);
                    double w = prev_center + p * (curr_center - prev_center);
                    return w / total_weight_;
                }
            }
            cur_weight += centroids_[i].weight;
        }

        std::size_t last_idx = centroids_.size() - 1;
        double p = (x - centroids_[last_idx].mean) / (max_val_ - centroids_[last_idx].mean);
        double w = (total_weight_ - centroids_[last_idx].weight / 2.0) + p * (centroids_[last_idx].weight / 2.0);
        return w / total_weight_;
    }

    // =========================================================================
    // Serialization
    // =========================================================================

    void serialize(std::ostream& out) const {
        compress();
        out.write("PTD1", 4);
        out.write(reinterpret_cast<const char*>(&compression_), sizeof(compression_));
        out.write(reinterpret_cast<const char*>(&total_weight_), sizeof(total_weight_));
        out.write(reinterpret_cast<const char*>(&min_val_), sizeof(min_val_));
        out.write(reinterpret_cast<const char*>(&max_val_), sizeof(max_val_));
        
        std::uint64_t size = centroids_.size();
        out.write(reinterpret_cast<const char*>(&size), sizeof(size));
        out.write(reinterpret_cast<const char*>(centroids_.data()),
                  static_cast<std::streamsize>(size * sizeof(Centroid)));
        if (!out) {
            throw std::runtime_error("tDigest::serialize: write failed");
        }
    }

    static tDigest deserialize(std::istream& in) {
        char magic[4];
        in.read(magic, 4);
        if (std::memcmp(magic, "PTD1", 4) != 0) {
            throw std::runtime_error("tDigest::deserialize: invalid magic number");
        }

        double compression, total_weight, min_val, max_val;
        in.read(reinterpret_cast<char*>(&compression), sizeof(compression));
        in.read(reinterpret_cast<char*>(&total_weight), sizeof(total_weight));
        in.read(reinterpret_cast<char*>(&min_val), sizeof(min_val));
        in.read(reinterpret_cast<char*>(&max_val), sizeof(max_val));

        std::uint64_t size;
        in.read(reinterpret_cast<char*>(&size), sizeof(size));

        std::vector<Centroid> centroids(size);
        in.read(reinterpret_cast<char*>(centroids.data()),
                static_cast<std::streamsize>(size * sizeof(Centroid)));

        if (!in) {
            throw std::runtime_error("tDigest::deserialize: read failed");
        }

        tDigest td(compression);
        td.total_weight_ = total_weight;
        td.min_val_ = min_val;
        td.max_val_ = max_val;
        td.centroids_ = std::move(centroids);
        td.dirty_ = false;
        return td;
    }

    // =========================================================================
    // Statistics & Introspection
    // =========================================================================

    [[nodiscard]] double compression() const noexcept { return compression_; }
    [[nodiscard]] double total_weight() const noexcept { return total_weight_; }
    [[nodiscard]] std::size_t size() const noexcept { return centroids_.size(); }
    [[nodiscard]] double min() const noexcept { return min_val_; }
    [[nodiscard]] double max() const noexcept { return max_val_; }
    [[nodiscard]] std::size_t memory_bytes() const noexcept {
        return sizeof(tDigest) + centroids_.capacity() * sizeof(Centroid);
    }

    const std::vector<Centroid>& centroids() const noexcept {
        compress();
        return centroids_;
    }

    void clear() noexcept {
        centroids_.clear();
        total_weight_ = 0.0;
        min_val_ = std::numeric_limits<double>::max();
        max_val_ = -std::numeric_limits<double>::max();
        dirty_ = false;
    }

private:
    double compression_;
    mutable double total_weight_;
    mutable double min_val_;
    mutable double max_val_;
    mutable std::vector<Centroid> centroids_;
    mutable bool dirty_;
};

} // namespace probds
