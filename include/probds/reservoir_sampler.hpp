#pragma once
// =============================================================================
// reservoir_sampler.hpp — Reservoir Sampler
// probDS: High-Performance Probabilistic Data Structures Library
// =============================================================================

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <istream>
#include <ostream>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace probds {

template <typename T = std::string>
class ReservoirSampler {
public:
    // =========================================================================
    // Construction
    // =========================================================================

    explicit ReservoirSampler(std::size_t k, std::uint64_t seed = 12345ULL)
        : k_(k), n_(0), rng_(seed)
    {
        if (k == 0) {
            throw std::invalid_argument("ReservoirSampler: k must be > 0");
        }
    }

    // =========================================================================
    // Core Operations
    // =========================================================================

    /// Insert an item into the reservoir
    void insert(const T& item) {
        ++n_;
        if (samples_.size() < k_) {
            samples_.push_back(item);
        } else {
            // Generate random index in [0, n_ - 1]
            std::uniform_int_distribution<std::uint64_t> dist(0, n_ - 1);
            std::uint64_t j = dist(rng_);
            if (j < k_) {
                samples_[j] = item;
            }
        }
    }

    /// Insert an item into the reservoir (move overload)
    void insert(T&& item) {
        ++n_;
        if (samples_.size() < k_) {
            samples_.push_back(std::move(item));
        } else {
            std::uniform_int_distribution<std::uint64_t> dist(0, n_ - 1);
            std::uint64_t j = dist(rng_);
            if (j < k_) {
                samples_[j] = std::move(item);
            }
        }
    }

    /// Merge another reservoir sampler
    void merge(const ReservoirSampler& other) {
        if (other.n_ == 0) return;
        if (n_ == 0) {
            samples_ = other.samples_;
            n_ = other.n_;
            return;
        }

        std::uint64_t total_n = n_ + other.n_;
        if (total_n <= k_) {
            samples_.insert(samples_.end(), other.samples_.begin(), other.samples_.end());
            n_ = total_n;
            return;
        }

        // Weighted selection without replacement from both reservoirs
        auto A = samples_;
        auto B = other.samples_;
        
        std::shuffle(A.begin(), A.end(), rng_);
        
        // Shuffle B's copy
        auto B_shuffled = B;
        // Seeded locally or using our rng
        std::shuffle(B_shuffled.begin(), B_shuffled.end(), rng_);

        std::vector<T> merged;
        merged.reserve(k_);

        std::size_t idx_a = 0;
        std::size_t idx_b = 0;

        std::uniform_real_distribution<double> dist(0.0, 1.0);
        while (merged.size() < k_) {
            if (idx_a < A.size() && idx_b < B_shuffled.size()) {
                double prob_a = static_cast<double>(n_) / static_cast<double>(total_n);
                if (dist(rng_) < prob_a) {
                    merged.push_back(std::move(A[idx_a++]));
                } else {
                    merged.push_back(std::move(B_shuffled[idx_b++]));
                }
            } else if (idx_a < A.size()) {
                merged.push_back(std::move(A[idx_a++]));
            } else if (idx_b < B_shuffled.size()) {
                merged.push_back(std::move(B_shuffled[idx_b++]));
            } else {
                break;
            }
        }

        samples_ = std::move(merged);
        n_ = total_n;
    }

    // =========================================================================
    // Serialization
    // =========================================================================

    void serialize(std::ostream& out) const {
        out.write("PRS1", 4);
        write_u64(out, k_);
        write_u64(out, n_);
        
        // Serialize RNG state
        out << rng_ << "\n";

        write_u64(out, samples_.size());
        for (const auto& sample : samples_) {
            if constexpr (std::is_same_v<T, std::string>) {
                write_u64(out, sample.size());
                out.write(sample.data(), static_cast<std::streamsize>(sample.size()));
            } else {
                write_u64(out, sizeof(T));
                out.write(reinterpret_cast<const char*>(&sample), sizeof(T));
            }
        }
        if (!out) {
            throw std::runtime_error("ReservoirSampler::serialize: write failed");
        }
    }

    static ReservoirSampler deserialize(std::istream& in) {
        char magic[4];
        in.read(magic, 4);
        if (std::memcmp(magic, "PRS1", 4) != 0) {
            throw std::runtime_error("ReservoirSampler::deserialize: invalid magic number");
        }

        const auto k = read_u64(in);
        const auto n = read_u64(in);

        ReservoirSampler sampler(k);
        sampler.n_ = n;

        // Deserialize RNG state
        in >> sampler.rng_;
        // Consume the trailing newline
        char c;
        in.get(c);

        const auto size = read_u64(in);
        sampler.samples_.resize(size);

        for (std::size_t i = 0; i < size; ++i) {
            const auto len = read_u64(in);
            if constexpr (std::is_same_v<T, std::string>) {
                std::string s(len, '\0');
                in.read(&s[0], static_cast<std::streamsize>(len));
                sampler.samples_[i] = std::move(s);
            } else {
                T val;
                in.read(reinterpret_cast<char*>(&val), sizeof(T));
                sampler.samples_[i] = std::move(val);
            }
        }

        if (!in) {
            throw std::runtime_error("ReservoirSampler::deserialize: read failed");
        }

        return sampler;
    }

    // =========================================================================
    // Statistics & Introspection
    // =========================================================================

    [[nodiscard]] std::size_t k() const noexcept { return k_; }
    [[nodiscard]] std::uint64_t total_processed() const noexcept { return n_; }
    [[nodiscard]] std::size_t size() const noexcept { return samples_.size(); }
    [[nodiscard]] std::size_t memory_bytes() const noexcept {
        std::size_t total = sizeof(ReservoirSampler) + samples_.capacity() * sizeof(T);
        if constexpr (std::is_same_v<T, std::string>) {
            for (const auto& s : samples_) {
                total += s.capacity();
            }
        }
        return total;
    }

    const std::vector<T>& get_samples() const noexcept { return samples_; }

    void clear() noexcept {
        samples_.clear();
        n_ = 0;
    }

private:
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
    std::mt19937_64 rng_;
    std::vector<T> samples_;
};

} // namespace probds
