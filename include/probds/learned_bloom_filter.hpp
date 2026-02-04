#pragma once
// =============================================================================
// learned_bloom_filter.hpp — Learned Bloom Filter
// probDS: High-Performance Probabilistic Data Structures Library
// =============================================================================

#include "hash.hpp"
#include "bloom_filter.hpp"
#include <functional>
#include <istream>
#include <ostream>
#include <utility>

namespace probds {

template <
    typename T = std::string_view,
    typename Classifier = std::function<bool(const T&)>,
    typename Hash = DefaultHash<T>
>
class LearnedBloomFilter {
public:
    static_assert(is_hash_policy_v<Hash, T>, "Hash must satisfy the HashPolicy concept");

    // =========================================================================
    // Construction
    // =========================================================================

    LearnedBloomFilter(Classifier classifier,
                       double backup_fpr,
                       std::size_t expected_false_negatives,
                       Hash hasher = Hash{})
        : count_(0),
          classifier_(std::move(classifier)),
          backup_bloom_(expected_false_negatives == 0 ? 1 : expected_false_negatives, backup_fpr, std::move(hasher))
    {}

    // =========================================================================
    // Core Operations
    // =========================================================================

    /// Insert an item
    void insert(const T& key) {
        if (!classifier_(key)) {
            backup_bloom_.insert(key);
        }
        ++count_;
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
        if (classifier_(key)) {
            return true;
        }
        return backup_bloom_.possibly_contains(key);
    }

    void clear() noexcept {
        backup_bloom_.clear();
        count_ = 0;
    }

    // =========================================================================
    // Statistics & Introspection
    // =========================================================================

    [[nodiscard]] std::size_t size() const noexcept { return count_; }
    [[nodiscard]] std::size_t memory_bytes() const noexcept {
        return sizeof(LearnedBloomFilter) + backup_bloom_.memory_bytes();
    }
    [[nodiscard]] std::size_t memory_usage() const noexcept { return memory_bytes(); }

    [[nodiscard]] const BloomFilter<T, Hash>& backup_bloom() const noexcept {
        return backup_bloom_;
    }

    // =========================================================================
    // Serialization
    // =========================================================================

    void serialize(std::ostream& out) const {
        backup_bloom_.serialize(out);
    }

    static LearnedBloomFilter deserialize(std::istream& in, Classifier classifier, Hash hasher = Hash{}) {
        auto backup = BloomFilter<T, Hash>::deserialize(in, std::move(hasher));
        return LearnedBloomFilter(std::move(classifier), std::move(backup));
    }

private:
    // Constructor for deserialization
    LearnedBloomFilter(Classifier classifier, BloomFilter<T, Hash> backup_bloom)
        : count_(backup_bloom.size()), // Use backup size as best estimate if not tracked
          classifier_(std::move(classifier)),
          backup_bloom_(std::move(backup_bloom))
    {}

    std::size_t count_;
    Classifier classifier_;
    BloomFilter<T, Hash> backup_bloom_;
};

} // namespace probds
