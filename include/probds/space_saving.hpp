#pragma once
// =============================================================================
// space_saving.hpp — Space-Saving Top-K Sketch (Stream-Summary O(1))
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
class SpaceSaving {
public:
    static_assert(is_hash_policy_v<Hash, T>, "Hash must satisfy the HashPolicy concept");

    struct Entry {
        T key;
        std::size_t count;
        std::size_t error;
    };

    struct Bucket;

    struct ItemNode {
        T key;
        std::size_t error;
        Bucket* parent;
        ItemNode* prev;
        ItemNode* next;
    };

    struct Bucket {
        std::size_t count;
        ItemNode* item_head;
        ItemNode* item_tail;
        Bucket* prev;
        Bucket* next;
    };

    // =========================================================================
    // Construction & Destruction
    // =========================================================================

    explicit SpaceSaving(std::size_t k, Hash hasher = Hash{})
        : k_(k), count_(0), hasher_(std::move(hasher)),
          bucket_head_(nullptr), bucket_tail_(nullptr), cache_dirty_(true)
    {
        if (k == 0) {
            throw std::invalid_argument("SpaceSaving: k must be > 0");
        }
    }

    ~SpaceSaving() {
        destroy();
    }

    SpaceSaving(const SpaceSaving& other)
        : k_(other.k_), count_(other.count_), hasher_(other.hasher_),
          bucket_head_(nullptr), bucket_tail_(nullptr), cache_dirty_(true)
    {
        copy_from(other);
    }

    SpaceSaving& operator=(const SpaceSaving& other) {
        if (this != &other) {
            destroy();
            k_ = other.k_;
            count_ = other.count_;
            hasher_ = other.hasher_;
            cache_dirty_ = true;
            copy_from(other);
        }
        return *this;
    }

    SpaceSaving(SpaceSaving&& other) noexcept
        : k_(other.k_), count_(other.count_), hasher_(std::move(other.hasher_)),
          bucket_head_(other.bucket_head_), bucket_tail_(other.bucket_tail_),
          index_(std::move(other.index_)), key_storage_(std::move(other.key_storage_)),
          cached_entries_(std::move(other.cached_entries_)), cache_dirty_(other.cache_dirty_)
    {
        other.bucket_head_ = nullptr;
        other.bucket_tail_ = nullptr;
        other.count_ = 0;
    }

    SpaceSaving& operator=(SpaceSaving&& other) noexcept {
        if (this != &other) {
            destroy();
            k_ = other.k_;
            count_ = other.count_;
            hasher_ = std::move(other.hasher_);
            bucket_head_ = other.bucket_head_;
            bucket_tail_ = other.bucket_tail_;
            index_ = std::move(other.index_);
            key_storage_ = std::move(other.key_storage_);
            cached_entries_ = std::move(other.cached_entries_);
            cache_dirty_ = other.cache_dirty_;

            other.bucket_head_ = nullptr;
            other.bucket_tail_ = nullptr;
            other.count_ = 0;
        }
        return *this;
    }

    // =========================================================================
    // Core Operations
    // =========================================================================

    /// Insert an item with an optional count
    void insert(const T& key, std::size_t count = 1) {
        if (count == 0) return;
        count_ += count;
        cache_dirty_ = true;

        auto it = index_.find(key);
        if (it != index_.end()) {
            ItemNode* node = it->second;
            Bucket* old_bucket = node->parent;
            std::size_t new_count = old_bucket->count + count;

            Bucket* new_bucket = find_or_create_bucket(old_bucket, new_count);
            remove_item_from_bucket(node);
            add_item_to_bucket(new_bucket, node);
            return;
        }

        if (index_.size() < k_) {
            T stored_key = copy_key(key);
            ItemNode* node = new ItemNode{stored_key, 0, nullptr, nullptr, nullptr};
            Bucket* b = find_or_create_bucket(bucket_head_, count);
            add_item_to_bucket(b, node);
            index_[stored_key] = node;
            return;
        }

        // Table is full. Evict the minimum element (head of first bucket).
        ItemNode* min_node = bucket_head_->item_head;
        std::size_t min_count = bucket_head_->count;

        index_.erase(min_node->key);

        T stored_key = copy_key(key);
        min_node->key = stored_key;
        min_node->error = min_count;

        std::size_t new_count = min_count + count;
        Bucket* new_bucket = find_or_create_bucket(bucket_head_, new_count);

        remove_item_from_bucket(min_node);
        add_item_to_bucket(new_bucket, min_node);

        index_[stored_key] = min_node;
    }

    /// Estimate the frequency of key
    [[nodiscard]] std::size_t estimate(const T& key) const noexcept {
        auto it = index_.find(key);
        if (it != index_.end()) {
            return it->second->parent->count;
        }
        return index_.size() == k_ ? get_min_count() : 0;
    }

    /// Estimate upper bound on error of key
    [[nodiscard]] std::size_t error(const T& key) const noexcept {
        auto it = index_.find(key);
        if (it != index_.end()) {
            return it->second->error;
        }
        return index_.size() == k_ ? get_min_count() : 0;
    }

    /// Get top-K frequent elements in descending order
    [[nodiscard]] std::vector<std::pair<T, std::size_t>> top_k(std::size_t top) const {
        std::vector<std::pair<T, std::size_t>> result;
        result.reserve(index_.size());

        // Iterate through buckets from tail to head (descending count)
        Bucket* curr_b = bucket_tail_;
        while (curr_b) {
            ItemNode* curr_i = curr_b->item_head;
            while (curr_i) {
                result.push_back({curr_i->key, curr_b->count});
                curr_i = curr_i->next;
            }
            curr_b = curr_b->prev;
        }

        if (result.size() > top) {
            result.resize(top);
        }
        return result;
    }

    /// Merge another SpaceSaving sketch
    void merge(const SpaceSaving& other) {
        auto other_entries = other.entries();
        for (auto rit = other_entries.rbegin(); rit != other_entries.rend(); ++rit) {
            insert(rit->key, rit->count);
        }
    }

    // =========================================================================
    // Serialization
    // =========================================================================

    void serialize(std::ostream& out) const {
        out.write("PSS1", 4);
        out.write(reinterpret_cast<const char*>(&k_), sizeof(k_));
        out.write(reinterpret_cast<const char*>(&count_), sizeof(count_));
        
        std::uint64_t size = index_.size();
        out.write(reinterpret_cast<const char*>(&size), sizeof(size));
        
        auto sorted = entries(); // Get sorted entries for binary compatibility
        for (const auto& entry : sorted) {
            auto sv = detail::to_string_view(entry.key);
            std::uint64_t len = sv.size();
            out.write(reinterpret_cast<const char*>(&len), sizeof(len));
            out.write(sv.data(), static_cast<std::streamsize>(len));
            out.write(reinterpret_cast<const char*>(&entry.count), sizeof(entry.count));
            out.write(reinterpret_cast<const char*>(&entry.error), sizeof(entry.error));
        }
        if (!out) {
            throw std::runtime_error("SpaceSaving::serialize: write failed");
        }
    }

    static SpaceSaving deserialize(std::istream& in, Hash hasher = Hash{}) {
        char magic[4];
        in.read(magic, 4);
        if (std::memcmp(magic, "PSS1", 4) != 0) {
            throw std::runtime_error("SpaceSaving::deserialize: invalid magic number");
        }

        std::uint64_t k, count, size;
        in.read(reinterpret_cast<char*>(&k), sizeof(k));
        in.read(reinterpret_cast<char*>(&count), sizeof(count));
        in.read(reinterpret_cast<char*>(&size), sizeof(size));

        SpaceSaving ss(k, std::move(hasher));
        ss.count_ = count;

        Bucket* last_bucket = ss.bucket_head_;
        for (std::size_t i = 0; i < size; ++i) {
            std::uint64_t len;
            in.read(reinterpret_cast<char*>(&len), sizeof(len));
            std::vector<char> buf(len);
            in.read(buf.data(), static_cast<std::streamsize>(len));
            std::uint64_t entry_count, entry_error;
            in.read(reinterpret_cast<char*>(&entry_count), sizeof(entry_count));
            in.read(reinterpret_cast<char*>(&entry_error), sizeof(entry_error));

            T key;
            if constexpr (std::is_same_v<T, std::string>) {
                key = std::string(buf.data(), len);
            } else if constexpr (std::is_same_v<T, std::string_view>) {
                ss.key_storage_.push_back(std::string(buf.data(), len));
                key = ss.key_storage_.back();
            } else {
                std::memcpy(&key, buf.data(), sizeof(T));
            }

            ItemNode* node = new ItemNode{key, entry_error, nullptr, nullptr, nullptr};
            ss.index_[key] = node;

            Bucket* b = ss.find_or_create_bucket(last_bucket, entry_count);
            ss.add_item_to_bucket(b, node);
            last_bucket = b;
        }

        if (!in) {
            throw std::runtime_error("SpaceSaving::deserialize: read failed");
        }

        ss.cache_dirty_ = true;
        return ss;
    }

    // =========================================================================
    // Statistics & Introspection
    // =========================================================================

    [[nodiscard]] std::size_t k() const noexcept { return k_; }
    [[nodiscard]] std::size_t size() const noexcept { return count_; }
    
    [[nodiscard]] std::size_t memory_bytes() const noexcept {
        std::size_t total = sizeof(SpaceSaving);
        total += index_.bucket_count() * sizeof(void*) + index_.size() * (sizeof(T) + sizeof(void*) + 32);
        for (const auto& str : key_storage_) {
            total += str.capacity();
        }
        Bucket* curr_b = bucket_head_;
        while (curr_b) {
            total += sizeof(Bucket);
            ItemNode* curr_i = curr_b->item_head;
            while (curr_i) {
                total += sizeof(ItemNode);
                curr_i = curr_i->next;
            }
            curr_b = curr_b->next;
        }
        return total;
    }

    const std::vector<Entry>& entries() const noexcept {
        if (cache_dirty_) {
            cached_entries_.clear();
            cached_entries_.reserve(index_.size());
            Bucket* curr_b = bucket_head_;
            while (curr_b) {
                ItemNode* curr_i = curr_b->item_head;
                while (curr_i) {
                    cached_entries_.push_back(Entry{curr_i->key, curr_b->count, curr_i->error});
                    curr_i = curr_i->next;
                }
                curr_b = curr_b->next;
            }
            cache_dirty_ = false;
        }
        return cached_entries_;
    }

    void clear() noexcept {
        destroy();
    }

private:
    [[nodiscard]] std::size_t get_min_count() const noexcept {
        return bucket_head_ ? bucket_head_->count : 0;
    }

    T copy_key(const T& key) {
        if constexpr (std::is_same_v<T, std::string_view>) {
            key_storage_.push_back(std::string(key));
            return key_storage_.back();
        } else {
            return key;
        }
    }

    void destroy() {
        Bucket* curr_b = bucket_head_;
        while (curr_b) {
            ItemNode* curr_i = curr_b->item_head;
            while (curr_i) {
                ItemNode* next_i = curr_i->next;
                delete curr_i;
                curr_i = next_i;
            }
            Bucket* next_b = curr_b->next;
            delete curr_b;
            curr_b = next_b;
        }
        bucket_head_ = nullptr;
        bucket_tail_ = nullptr;
        index_.clear();
        key_storage_.clear();
        count_ = 0;
        cache_dirty_ = true;
    }

    void copy_from(const SpaceSaving& other) {
        Bucket* other_b = other.bucket_head_;
        while (other_b) {
            Bucket* new_b = new Bucket{other_b->count, nullptr, nullptr, bucket_tail_, nullptr};
            if (bucket_tail_) {
                bucket_tail_->next = new_b;
            } else {
                bucket_head_ = new_b;
            }
            bucket_tail_ = new_b;

            ItemNode* other_i = other_b->item_head;
            while (other_i) {
                T new_key = copy_key(other_i->key);
                ItemNode* new_i = new ItemNode{new_key, other_i->error, new_b, nullptr, nullptr};
                if (!new_b->item_head) {
                    new_b->item_head = new_i;
                    new_b->item_tail = new_i;
                } else {
                    new_b->item_tail->next = new_i;
                    new_i->prev = new_b->item_tail;
                    new_b->item_tail = new_i;
                }
                index_[new_key] = new_i;
                other_i = other_i->next;
            }
            other_b = other_b->next;
        }
    }

    Bucket* find_or_create_bucket(Bucket* start_from, std::size_t target_count) {
        if (!start_from) {
            Bucket* new_bucket = new Bucket{target_count, nullptr, nullptr, nullptr, nullptr};
            bucket_head_ = new_bucket;
            bucket_tail_ = new_bucket;
            return new_bucket;
        }
        Bucket* curr = start_from;
        while (curr && curr->count < target_count) {
            if (!curr->next || curr->next->count > target_count) {
                Bucket* new_bucket = new Bucket{target_count, nullptr, nullptr, curr, curr->next};
                if (curr->next) {
                    curr->next->prev = new_bucket;
                } else {
                    bucket_tail_ = new_bucket;
                }
                curr->next = new_bucket;
                return new_bucket;
            }
            curr = curr->next;
        }
        if (curr && curr->count == target_count) {
            return curr;
        }
        if (curr && curr->count > target_count) {
            Bucket* new_bucket = new Bucket{target_count, nullptr, nullptr, curr->prev, curr};
            if (curr->prev) {
                curr->prev->next = new_bucket;
            } else {
                bucket_head_ = new_bucket;
            }
            curr->prev = new_bucket;
            return new_bucket;
        }
        return nullptr;
    }

    void remove_item_from_bucket(ItemNode* node) {
        Bucket* b = node->parent;
        if (node->prev) {
            node->prev->next = node->next;
        } else {
            b->item_head = node->next;
        }
        if (node->next) {
            node->next->prev = node->prev;
        } else {
            b->item_tail = node->prev;
        }
        node->prev = nullptr;
        node->next = nullptr;
        node->parent = nullptr;

        if (!b->item_head) {
            if (b->prev) {
                b->prev->next = b->next;
            } else {
                bucket_head_ = b->next;
            }
            if (b->next) {
                b->next->prev = b->prev;
            } else {
                bucket_tail_ = b->prev;
            }
            delete b;
        }
    }

    void add_item_to_bucket(Bucket* b, ItemNode* node) {
        node->parent = b;
        node->next = nullptr;
        if (!b->item_head) {
            b->item_head = node;
            b->item_tail = node;
            node->prev = nullptr;
        } else {
            b->item_tail->next = node;
            node->prev = b->item_tail;
            b->item_tail = node;
        }
    }

    std::size_t k_;
    std::size_t count_;
    Hash hasher_;

    Bucket* bucket_head_ = nullptr;
    Bucket* bucket_tail_ = nullptr;
    std::unordered_map<T, ItemNode*, Hash> index_;
    std::list<std::string> key_storage_;
    mutable std::vector<Entry> cached_entries_;
    mutable bool cache_dirty_;
};

} // namespace probds
