#pragma once
// =============================================================================
// misra_gries.hpp — Misra-Gries Frequent Items sketch
// probDS: High-Performance Probabilistic Data Structures Library
// =============================================================================

#include "hash.hpp"
#include <algorithm>
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
class MisraGries {
public:
    static_assert(is_hash_policy_v<Hash, T>, "Hash must satisfy the HashPolicy concept");

    // =========================================================================
    // Construction
    // =========================================================================

    explicit MisraGries(std::size_t k, Hash hasher = Hash{})
        : k_(k), count_(0), hasher_(std::move(hasher))
    {
        if (k <= 1) {
            throw std::invalid_argument("MisraGries: k must be > 1");
        }
    }

    // =========================================================================
    // Core Operations
    // =========================================================================

    /// Insert an item with an optional count/weight
    void insert(const T& key, std::size_t count = 1) {
        if (count == 0) return;
        count_ += count;

        auto it = counts_.find(key);
        if (it != counts_.end()) {
            it->second += count;
            return;
        }

        if (counts_.size() < k_ - 1) {
            counts_[key] = count;
            return;
        }

        // Find the minimum count in the current map
        std::size_t min_val = std::numeric_limits<std::size_t>::max();
        for (const auto& pair : counts_) {
            min_val = std::min(min_val, pair.second);
        }

        if (count < min_val) {
            // Subtract count from all existing entries
            auto curr_it = counts_.begin();
            while (curr_it != counts_.end()) {
                curr_it->second -= count;
                if (curr_it->second == 0) {
                    curr_it = counts_.erase(curr_it);
                } else {
                    ++curr_it;
                }
            }
        } else {
            // Subtract min_val from all entries
            auto curr_it = counts_.begin();
            while (curr_it != counts_.end()) {
                curr_it->second -= min_val;
                if (curr_it->second == 0) {
                    curr_it = counts_.erase(curr_it);
                } else {
                    ++curr_it;
                }
            }
            // Insert the new key with the remaining count
            counts_[key] = count - min_val;
        }
    }

    /// Estimate the frequency of key
    [[nodiscard]] std::size_t estimate(const T& key) const noexcept {
        auto it = counts_.find(key);
        if (it != counts_.end()) {
            return it->second;
        }
        return 0;
    }

    /// Return keys with estimated frequency >= threshold
    [[nodiscard]] std::vector<std::pair<T, std::size_t>> heavy_hitters(std::size_t threshold) const {
        std::vector<std::pair<T, std::size_t>> result;
        for (const auto& pair : counts_) {
            if (pair.second >= threshold) {
                result.push_back(pair);
            }
        }
        return result;
    }

    /// Merge another MisraGries sketch into this one
    void merge(const MisraGries& other) {
        // Simple merge: insert other map's keys and counts
        for (const auto& pair : other.counts_) {
            insert(pair.first, pair.second);
        }
    }

    // =========================================================================
    // Serialization
    // =========================================================================

    void serialize(std::ostream& out) const {
        out.write("PMG1", 4);
        write_u64(out, k_);
        write_u64(out, count_);
        write_u64(out, counts_.size());
        for (const auto& pair : counts_) {
            auto sv = detail::to_string_view(pair.first);
            write_u64(out, sv.size());
            out.write(sv.data(), static_cast<std::streamsize>(sv.size()));
            write_u64(out, pair.second);
        }
        if (!out) {
            throw std::runtime_error("MisraGries::serialize: write failed");
        }
    }

    static MisraGries deserialize(std::istream& in, Hash hasher = Hash{}) {
        char magic[4];
        in.read(magic, 4);
        if (std::memcmp(magic, "PMG1", 4) != 0) {
            throw std::runtime_error("MisraGries::deserialize: invalid magic number");
        }

        const auto k = read_u64(in);
        const auto count = read_u64(in);
        const auto size = read_u64(in);

        MisraGries mg(k, std::move(hasher));
        mg.count_ = count;

        for (std::size_t i = 0; i < size; ++i) {
            const auto len = read_u64(in);
            std::vector<char> buf(len);
            in.read(buf.data(), static_cast<std::streamsize>(len));
            const auto val = read_u64(in);

            if constexpr (std::is_same_v<T, std::string>) {
                std::string key(buf.data(), len);
                mg.counts_[key] = val;
            } else if constexpr (std::is_same_v<T, std::string_view>) {
                mg.key_storage_.push_back(std::string(buf.data(), len));
                mg.counts_[mg.key_storage_.back()] = val;
            } else {
                T key;
                std::memcpy(&key, buf.data(), sizeof(T));
                mg.counts_[key] = val;
            }
        }

        if (!in) {
            throw std::runtime_error("MisraGries::deserialize: read failed");
        }

        return mg;
    }

    // =========================================================================
    // Statistics & Introspection
    // =========================================================================

    [[nodiscard]] std::size_t k() const noexcept { return k_; }
    [[nodiscard]] std::size_t size() const noexcept { return count_; }
    [[nodiscard]] std::size_t memory_bytes() const noexcept {
        std::size_t total = sizeof(MisraGries) + counts_.bucket_count() * sizeof(void*);
        // Approximation of node size in std::unordered_map
        total += counts_.size() * (sizeof(T) + sizeof(std::size_t) + 32);
        for (const auto& str : key_storage_) {
            total += str.capacity();
        }
        return total;
    }

    const std::unordered_map<T, std::size_t, Hash>& counts() const noexcept {
        return counts_;
    }

    void clear() noexcept {
        counts_.clear();
        key_storage_.clear();
        count_ = 0;
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
    std::size_t count_;
    Hash hasher_;
    std::unordered_map<T, std::size_t, Hash> counts_;
    std::list<std::string> key_storage_; // Persistent storage for string_views during deserialization
};

} // namespace probds
