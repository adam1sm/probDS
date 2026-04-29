#pragma once
// =============================================================================
// exponential_histogram.hpp — Sliding Window Frequency Estimator (DGIM)
// probDS: High-Performance Probabilistic Data Structures Library
// =============================================================================

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <istream>
#include <list>
#include <ostream>
#include <stdexcept>

namespace probds {

class ExponentialHistogram {
public:
    struct Bucket {
        std::uint64_t size;
        std::uint64_t timestamp;
    };

    // =========================================================================
    // Construction
    // =========================================================================

    explicit ExponentialHistogram(std::size_t N, double epsilon = 0.1)
        : N_(N), epsilon_(epsilon), current_time_(0)
    {
        if (N == 0) {
            throw std::invalid_argument("ExponentialHistogram: N must be > 0");
        }
        if (epsilon <= 0.0 || epsilon >= 1.0) {
            throw std::invalid_argument("ExponentialHistogram: epsilon must be in (0, 1)");
        }
        
        M_ = static_cast<std::size_t>(std::ceil(1.0 / epsilon));
        if (M_ < 2) M_ = 2;
    }

    ~ExponentialHistogram() = default;

    ExponentialHistogram(const ExponentialHistogram&) = default;
    ExponentialHistogram& operator=(const ExponentialHistogram&) = default;
    ExponentialHistogram(ExponentialHistogram&&) noexcept = default;
    ExponentialHistogram& operator=(ExponentialHistogram&&) noexcept = default;

    // =========================================================================
    // Core Operations
    // =========================================================================

    /// Update with a new stream bit
    void update(bool bit) {
        current_time_++;
        
        // 1. Expire old buckets
        while (!buckets_.empty() && buckets_.back().timestamp + N_ < current_time_) {
            buckets_.pop_back();
        }
        
        if (bit) {
            // 2. Insert new bucket of size 1
            buckets_.push_front(Bucket{1, current_time_});
            
            // 3. Merge buckets recursively if size limit K is exceeded
            std::size_t K = M_ / 2 + 1;
            if (K < 2) K = 2;
            
            auto it = buckets_.begin();
            while (it != buckets_.end()) {
                std::size_t current_size = it->size;
                std::size_t count = 0;
                
                while (it != buckets_.end() && it->size == current_size) {
                    count++;
                    ++it;
                }
                
                if (count > K) {
                    // Merge the two oldest buckets of this size
                    auto oldest_it = std::prev(it);
                    auto second_oldest_it = std::prev(oldest_it);
                    
                    std::uint64_t merged_timestamp = second_oldest_it->timestamp;
                    std::uint64_t new_size = 2 * current_size;
                    
                    // Erase both and insert new one
                    auto insert_pos = buckets_.erase(second_oldest_it, it);
                    buckets_.insert(insert_pos, Bucket{new_size, merged_timestamp});
                    
                    // Restart scan from the beginning to check for cascades
                    it = buckets_.begin();
                }
            }
        }
    }

    /// Estimate the count of 1s in the last n elements
    [[nodiscard]] std::uint64_t estimate_last_n(std::size_t n) const noexcept {
        if (n == 0) return 0;
        if (n > N_) n = N_;
        
        std::uint64_t sum = 0;
        std::uint64_t limit = (current_time_ >= n) ? (current_time_ - n + 1) : 0;
        
        for (auto it = buckets_.begin(); it != buckets_.end(); ++it) {
            if (it->timestamp >= limit) {
                auto next_it = std::next(it);
                if ((next_it != buckets_.end() && next_it->timestamp >= limit) || limit <= 1) {
                    sum += it->size;
                } else {
                    // This is the oldest bucket inside the window
                    if (it->size == 1) {
                        sum += 1;
                    } else {
                        sum += it->size / 2;
                    }
                    break;
                }
            } else {
                break;
            }
        }
        return sum;
    }

    void clear() noexcept {
        buckets_.clear();
        current_time_ = 0;
    }

    // =========================================================================
    // Statistics & Introspection
    // =========================================================================

    [[nodiscard]] std::size_t window_size() const noexcept { return N_; }
    [[nodiscard]] double epsilon() const noexcept { return epsilon_; }
    [[nodiscard]] std::uint64_t current_time() const noexcept { return current_time_; }
    [[nodiscard]] std::size_t bucket_count() const noexcept { return buckets_.size(); }
    [[nodiscard]] const std::list<Bucket>& buckets() const noexcept { return buckets_; }

    [[nodiscard]] std::size_t memory_bytes() const noexcept {
        return buckets_.size() * sizeof(Bucket) + sizeof(ExponentialHistogram);
    }
    [[nodiscard]] std::size_t memory_usage() const noexcept { return memory_bytes(); }

    // =========================================================================
    // Serialization
    // =========================================================================

    void serialize(std::ostream& out) const {
        out.write("PEH1", 4);
        write_u64(out, N_);
        write_double(out, epsilon_);
        write_u64(out, current_time_);
        write_u64(out, M_);
        
        std::uint64_t num_buckets = buckets_.size();
        write_u64(out, num_buckets);
        for (const auto& b : buckets_) {
            write_u64(out, b.size);
            write_u64(out, b.timestamp);
        }
        if (!out) {
            throw std::runtime_error("ExponentialHistogram::serialize: write failed");
        }
    }

    static ExponentialHistogram deserialize(std::istream& in) {
        char magic[4];
        in.read(magic, 4);
        if (std::memcmp(magic, "PEH1", 4) != 0) {
            throw std::runtime_error("ExponentialHistogram::deserialize: invalid magic number");
        }

        const auto N = read_u64(in);
        const auto epsilon = read_double(in);
        const auto current_time = read_u64(in);
        const auto M = read_u64(in);
        const auto num_buckets = read_u64(in);

        ExponentialHistogram eh(N, epsilon);
        eh.current_time_ = current_time;
        eh.M_ = M;
        
        for (std::uint64_t i = 0; i < num_buckets; ++i) {
            Bucket b;
            b.size = read_u64(in);
            b.timestamp = read_u64(in);
            eh.buckets_.push_back(b);
        }

        if (!in) {
            throw std::runtime_error("ExponentialHistogram::deserialize: read failed");
        }
        return eh;
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
    static void write_double(std::ostream& out, double v) {
        out.write(reinterpret_cast<const char*>(&v), sizeof(v));
    }
    static double read_double(std::istream& in) {
        double v;
        in.read(reinterpret_cast<char*>(&v), sizeof(v));
        return v;
    }

    std::size_t N_;
    double epsilon_;
    std::uint64_t current_time_;
    std::size_t M_;
    std::list<Bucket> buckets_;
};

} // namespace probds
