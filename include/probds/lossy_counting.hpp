#pragma once
// =============================================================================
// lossy_counting.hpp — Lossy Counting Sketch for Frequency Estimation
// probDS: High-Performance Probabilistic Data Structures Library
// =============================================================================

#include "hash.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <istream>
#include <list>
#include <ostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace probds {

template <typename T = std::string_view, typename Hash = DefaultHash<T>>
class LossyCounting {
public:
    static_assert(is_hash_policy_v<Hash, T>, "Hash must satisfy the HashPolicy concept");

    struct Entry {
        std::size_t count;
        std::size_t error;
    };

    // =========================================================================
    // Construction
    // =========================================================================

    explicit LossyCounting(double epsilon, Hash hasher = Hash{})
        : epsilon_(epsilon),
          w_(static_cast<std::size_t>(std::ceil(1.0 / epsilon))),
          n_(0),
          hasher_(std::move(hasher))
    {
        if (epsilon <= 0.0 || epsilon >= 1.0) {
            throw std::invalid_argument("LossyCounting: epsilon must be in (0, 1)");
        }
    }

    // =========================================================================
    // Core Operations
    // =========================================================================

    /// Insert an item into the sketch
    void insert(const T& key) {
        ++n_;
        const std::size_t current_bucket = (n_ + w_ - 1) / w_; // ceil(n_ / w_)

        auto it = counts_.find(key);
        if (it != counts_.end()) {
            it->second.count += 1;
        } else {
            counts_[key] = Entry{1, current_bucket - 1};
        }

        // Clean up at bucket boundaries
        if (n_ % w_ == 0) {
            prune(current_bucket);
        }
    }

    /// Estimate the frequency of key
    [[nodiscard]] std::size_t estimate(const T& key) const noexcept {
        auto it = counts_.find(key);
        if (it != counts_.end()) {
            return it->second.count;
        }
        return 0;
    }

    /// Estimate the maximum error of the frequency of key
    [[nodiscard]] std::size_t error(const T& key) const noexcept {
        auto it = counts_.find(key);
        if (it != counts_.end()) {
            return it->second.error;
        }
        return (n_ + w_ - 1) / w_ - 1;
    }

    /// Find heavy hitters with frequency >= (threshold_fraction - epsilon) * N
    [[nodiscard]] std::vector<std::pair<T, std::size_t>> heavy_hitters(double threshold_fraction) const {
        if (threshold_fraction < epsilon_) {
            throw std::invalid_argument("LossyCounting::heavy_hitters: threshold_fraction must be >= epsilon");
        }

        std::vector<std::pair<T, std::size_t>> result;
        const double threshold = (threshold_fraction - epsilon_) * static_cast<double>(n_);

        for (const auto& pair : counts_) {
            if (static_cast<double>(pair.second.count) >= threshold) {
                result.push_back({pair.first, pair.second.count});
            }
        }
        return result;
    }

    /// Merge another LossyCounting sketch into this one
    void merge(const LossyCounting& other) {
        if (std::abs(epsilon_ - other.epsilon_) > 1e-9) {
            throw std::invalid_argument("LossyCounting::merge: epsilon mismatch");
        }

        const std::size_t current_bucket_this = (n_ + w_ - 1) / w_;
        const std::size_t current_bucket_other = (other.n_ + other.w_ - 1) / other.w_;

        std::unordered_map<T, Entry, Hash> merged;
        merged.reserve(counts_.size() + other.counts_.size());

        // Process keys in this sketch
        for (const auto& pair : counts_) {
            const T& key = pair.first;
            const auto& e = pair.second;
            
            auto it_other = other.counts_.find(key);
            if (it_other != other.counts_.end()) {
                merged[key] = Entry{e.count + it_other->second.count, e.error + it_other->second.error};
            } else {
                merged[key] = Entry{e.count, e.error + current_bucket_other};
            }
        }

        // Process keys only in other sketch
        for (const auto& pair : other.counts_) {
            const T& key = pair.first;
            const auto& e = pair.second;
            if (counts_.find(key) == counts_.end()) {
                merged[key] = Entry{e.count, e.error + current_bucket_this};
            }
        }

        counts_ = std::move(merged);
        n_ += other.n_;

        const std::size_t new_bucket = (n_ + w_ - 1) / w_;
        prune(new_bucket);
    }

    // =========================================================================
    // Serialization
    // =========================================================================

    void serialize(std::ostream& out) const {
        out.write("PLC1", 4);
        out.write(reinterpret_cast<const char*>(&epsilon_), sizeof(epsilon_));
        write_u64(out, n_);
        write_u64(out, counts_.size());

        for (const auto& pair : counts_) {
            auto sv = detail::to_string_view(pair.first);
            write_u64(out, sv.size());
            out.write(sv.data(), static_cast<std::streamsize>(sv.size()));
            write_u64(out, pair.second.count);
            write_u64(out, pair.second.error);
        }
        if (!out) {
            throw std::runtime_error("LossyCounting::serialize: write failed");
        }
    }

    static LossyCounting deserialize(std::istream& in, Hash hasher = Hash{}) {
        char magic[4];
        in.read(magic, 4);
        if (std::memcmp(magic, "PLC1", 4) != 0) {
            throw std::runtime_error("LossyCounting::deserialize: invalid magic number");
        }

        double epsilon;
        in.read(reinterpret_cast<char*>(&epsilon), sizeof(epsilon));
        const auto n = read_u64(in);
        const auto size = read_u64(in);

        LossyCounting lc(epsilon, std::move(hasher));
        lc.n_ = n;

        for (std::size_t i = 0; i < size; ++i) {
            const auto len = read_u64(in);
            std::vector<char> buf(len);
            in.read(buf.data(), static_cast<std::streamsize>(len));
            const auto count = read_u64(in);
            const auto error = read_u64(in);

            if constexpr (std::is_same_v<T, std::string>) {
                std::string key(buf.data(), len);
                lc.counts_[key] = Entry{count, error};
            } else if constexpr (std::is_same_v<T, std::string_view>) {
                lc.key_storage_.push_back(std::string(buf.data(), len));
                lc.counts_[lc.key_storage_.back()] = Entry{count, error};
            } else {
                T key;
                std::memcpy(&key, buf.data(), sizeof(T));
                lc.counts_[key] = Entry{count, error};
            }
        }

        if (!in) {
            throw std::runtime_error("LossyCounting::deserialize: read failed");
        }

        return lc;
    }

    // =========================================================================
    // Statistics & Introspection
    // =========================================================================

    [[nodiscard]] double epsilon() const noexcept { return epsilon_; }
    [[nodiscard]] std::size_t bucket_width() const noexcept { return w_; }
    [[nodiscard]] std::size_t total_processed() const noexcept { return n_; }
    [[nodiscard]] std::size_t size() const noexcept { return n_; }
    [[nodiscard]] std::size_t memory_bytes() const noexcept {
        std::size_t total = sizeof(LossyCounting) + counts_.bucket_count() * sizeof(void*);
        total += counts_.size() * (sizeof(T) + sizeof(Entry) + 32);
        for (const auto& str : key_storage_) {
            total += str.capacity();
        }
        return total;
    }

    const std::unordered_map<T, Entry, Hash>& counts() const noexcept { return counts_; }

    void clear() noexcept {
        counts_.clear();
        key_storage_.clear();
        n_ = 0;
    }

private:
    void prune(std::size_t bucket) noexcept {
        auto it = counts_.begin();
        while (it != counts_.end()) {
            if (it->second.count + it->second.error <= bucket) {
                it = counts_.erase(it);
            } else {
                ++it;
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

    double epsilon_;
    std::size_t w_;
    std::size_t n_;
    Hash hasher_;
    std::unordered_map<T, Entry, Hash> counts_;
    std::list<std::string> key_storage_;
};

} // namespace probds
