#pragma once
// =============================================================================
// heavy_keeper.hpp — HeavyKeeper Top-K Sketch (Stream-Summary O(1))
// probDS: High-Performance Probabilistic Data Structures Library
// =============================================================================

#include "hash.hpp"
#include <algorithm>
#include <array>
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
class HeavyKeeper {
public:
    static_assert(is_hash_policy_v<Hash, T>, "Hash must satisfy the HashPolicy concept");

    struct Entry {
        T key;
        std::size_t count;
    };

    struct Bucket {
        std::uint32_t fingerprint = 0;
        std::uint32_t count = 0;
    };

    struct FreqBucket;

    struct ItemNode {
        T key;
        FreqBucket* parent;
        ItemNode* prev;
        ItemNode* next;
    };

    struct FreqBucket {
        std::size_t count;
        ItemNode* item_head;
        ItemNode* item_tail;
        FreqBucket* prev;
        FreqBucket* next;
    };

    class XorShift64 {
    public:
        explicit XorShift64(std::uint64_t seed = 88172645463325252ULL)
            : state_(seed == 0 ? 88172645463325252ULL : seed) {}
        std::uint64_t next() noexcept {
            std::uint64_t x = state_;
            x ^= x << 13;
            x ^= x >> 7;
            x ^= x << 17;
            state_ = x;
            return x;
        }
    private:
        std::uint64_t state_;
    };

    // =========================================================================
    // Construction & Destruction
    // =========================================================================

    HeavyKeeper(std::size_t d, std::size_t w, std::size_t k, Hash hasher = Hash{})
        : d_(d), w_(w), k_(k), hasher_(std::move(hasher)),
          table_(d, std::vector<Bucket>(w)),
          rng_(12345U), bucket_head_(nullptr), bucket_tail_(nullptr), cache_dirty_(true)
    {
        if (d == 0 || w == 0 || k == 0) {
            throw std::invalid_argument("HeavyKeeper: d, w, k must be > 0");
        }
    }

    ~HeavyKeeper() {
        destroy();
    }

    HeavyKeeper(const HeavyKeeper& other)
        : d_(other.d_), w_(other.w_), k_(other.k_), hasher_(other.hasher_),
          table_(other.table_), rng_(other.rng_),
          bucket_head_(nullptr), bucket_tail_(nullptr), cache_dirty_(true)
    {
        copy_from(other);
    }

    HeavyKeeper& operator=(const HeavyKeeper& other) {
        if (this != &other) {
            destroy();
            d_ = other.d_;
            w_ = other.w_;
            k_ = other.k_;
            hasher_ = other.hasher_;
            table_ = other.table_;
            rng_ = other.rng_;
            cache_dirty_ = true;
            copy_from(other);
        }
        return *this;
    }

    HeavyKeeper(HeavyKeeper&& other) noexcept
        : d_(other.d_), w_(other.w_), k_(other.k_), hasher_(std::move(other.hasher_)),
          table_(std::move(other.table_)), rng_(other.rng_),
          bucket_head_(other.bucket_head_), bucket_tail_(other.bucket_tail_),
          index_(std::move(other.index_)), key_storage_(std::move(other.key_storage_)),
          cached_entries_(std::move(other.cached_entries_)), cache_dirty_(other.cache_dirty_)
    {
        other.bucket_head_ = nullptr;
        other.bucket_tail_ = nullptr;
    }

    HeavyKeeper& operator=(HeavyKeeper&& other) noexcept {
        if (this != &other) {
            destroy();
            d_ = other.d_;
            w_ = other.w_;
            k_ = other.k_;
            hasher_ = std::move(other.hasher_);
            table_ = std::move(other.table_);
            rng_ = other.rng_;
            bucket_head_ = other.bucket_head_;
            bucket_tail_ = other.bucket_tail_;
            index_ = std::move(other.index_);
            key_storage_ = std::move(other.key_storage_);
            cached_entries_ = std::move(other.cached_entries_);
            cache_dirty_ = other.cache_dirty_;

            other.bucket_head_ = nullptr;
            other.bucket_tail_ = nullptr;
        }
        return *this;
    }

    // =========================================================================
    // Core Operations
    // =========================================================================

    /// Insert an item into the sketch
    void insert(const T& key) {
        cache_dirty_ = true;

        // 1. Hash key
        const std::uint64_t hash_val = hasher_(key);
        // Double hashing
        const std::uint64_t h1 = fmix64(hash_val);
        const std::uint64_t h2 = fmix64(hash_val ^ 0x6c62272e07bb0142ULL);

        // 32-bit fingerprint
        std::uint32_t fingerprint = static_cast<std::uint32_t>(fmix64(hash_val ^ 0x3ac5c612ULL));
        if (fingerprint == 0) fingerprint = 1;

        std::uint32_t max_count = 0;

        // 2. Update 2D Table
        for (std::size_t i = 0; i < d_; ++i) {
            std::size_t col = static_cast<std::size_t>((h1 + i * h2) % w_);
            auto& bucket = table_[i][col];

            if (bucket.count == 0) {
                bucket.fingerprint = fingerprint;
                bucket.count = 1;
                max_count = std::max(max_count, 1U);
            } else if (bucket.fingerprint == fingerprint) {
                bucket.count++;
                max_count = std::max(max_count, bucket.count);
            } else {
                // Collision and decay
                std::uint32_t threshold = get_decay_threshold(bucket.count);
                std::uint32_t r = static_cast<std::uint32_t>(rng_.next() & 0xFFFFFFFFULL);
                if (r < threshold) {
                    bucket.count--;
                    if (bucket.count == 0) {
                        bucket.fingerprint = fingerprint;
                        bucket.count = 1;
                        max_count = std::max(max_count, 1U);
                    }
                }
            }
        }

        // 3. Update Top-K Stream-Summary
        auto it = index_.find(key);
        if (it != index_.end()) {
            ItemNode* node = it->second;
            FreqBucket* old_bucket = node->parent;
            if (max_count == 0) {
                remove_item_from_bucket(node);
                delete node;
                index_.erase(it);
            } else if (old_bucket->count != max_count) {
                FreqBucket* new_bucket = find_or_create_bucket(max_count);
                remove_item_from_bucket(node);
                add_item_to_bucket(new_bucket, node);
            }
            return;
        }

        if (max_count == 0) {
            return;
        }

        if (index_.size() < k_) {
            T stored_key = copy_key(key);
            ItemNode* node = new ItemNode{stored_key, nullptr, nullptr, nullptr};
            FreqBucket* b = find_or_create_bucket(max_count);
            add_item_to_bucket(b, node);
            index_[stored_key] = node;
            return;
        }

        // Evict min element if max_count is strictly greater
        if (max_count > bucket_head_->count) {
            ItemNode* min_node = bucket_head_->item_head;
            index_.erase(min_node->key);

            T stored_key = copy_key(key);
            min_node->key = stored_key;

            FreqBucket* new_bucket = find_or_create_bucket(max_count);
            remove_item_from_bucket(min_node);
            add_item_to_bucket(new_bucket, min_node);

            index_[stored_key] = min_node;
        }
    }

    /// Estimate key frequency
    [[nodiscard]] std::size_t estimate(const T& key) const noexcept {
        auto it = index_.find(key);
        if (it != index_.end()) {
            return it->second->parent->count;
        }

        const std::uint64_t hash_val = hasher_(key);
        const std::uint64_t h1 = fmix64(hash_val);
        const std::uint64_t h2 = fmix64(hash_val ^ 0x6c62272e07bb0142ULL);
        std::uint32_t fingerprint = static_cast<std::uint32_t>(fmix64(hash_val ^ 0x3ac5c612ULL));
        if (fingerprint == 0) fingerprint = 1;

        std::uint32_t max_count = 0;
        for (std::size_t i = 0; i < d_; ++i) {
            std::size_t col = static_cast<std::size_t>((h1 + i * h2) % w_);
            const auto& bucket = table_[i][col];
            if (bucket.fingerprint == fingerprint) {
                max_count = std::max(max_count, bucket.count);
            }
        }
        return max_count;
    }

    /// Get top-K frequent elements in descending order
    [[nodiscard]] std::vector<std::pair<T, std::size_t>> top_k(std::size_t top) const {
        std::vector<std::pair<T, std::size_t>> result;
        result.reserve(index_.size());

        FreqBucket* curr_b = bucket_tail_;
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

    // =========================================================================
    // Serialization
    // =========================================================================

    void serialize(std::ostream& out) const {
        out.write("PHK1", 4);
        std::uint64_t d_val = d_;
        std::uint64_t w_val = w_;
        std::uint64_t k_val = k_;
        out.write(reinterpret_cast<const char*>(&d_val), sizeof(d_val));
        out.write(reinterpret_cast<const char*>(&w_val), sizeof(w_val));
        out.write(reinterpret_cast<const char*>(&k_val), sizeof(k_val));

        for (std::size_t i = 0; i < d_; ++i) {
            for (std::size_t j = 0; j < w_; ++j) {
                out.write(reinterpret_cast<const char*>(&table_[i][j].fingerprint), sizeof(std::uint32_t));
                out.write(reinterpret_cast<const char*>(&table_[i][j].count), sizeof(std::uint32_t));
            }
        }

        std::uint64_t map_size = index_.size();
        out.write(reinterpret_cast<const char*>(&map_size), sizeof(map_size));

        auto sorted = entries();
        for (const auto& entry : sorted) {
            auto sv = detail::to_string_view(entry.key);
            std::uint64_t len = sv.size();
            out.write(reinterpret_cast<const char*>(&len), sizeof(len));
            out.write(sv.data(), static_cast<std::streamsize>(len));
            std::uint64_t cnt = entry.count;
            out.write(reinterpret_cast<const char*>(&cnt), sizeof(cnt));
        }
        if (!out) {
            throw std::runtime_error("HeavyKeeper::serialize: write failed");
        }
    }

    static HeavyKeeper deserialize(std::istream& in, Hash hasher = Hash{}) {
        char magic[4];
        in.read(magic, 4);
        if (std::memcmp(magic, "PHK1", 4) != 0) {
            throw std::runtime_error("HeavyKeeper::deserialize: invalid magic number");
        }

        std::uint64_t d_val, w_val, k_val;
        in.read(reinterpret_cast<char*>(&d_val), sizeof(d_val));
        in.read(reinterpret_cast<char*>(&w_val), sizeof(w_val));
        in.read(reinterpret_cast<char*>(&k_val), sizeof(k_val));

        HeavyKeeper hk(d_val, w_val, k_val, std::move(hasher));

        for (std::size_t i = 0; i < d_val; ++i) {
            for (std::size_t j = 0; j < w_val; ++j) {
                in.read(reinterpret_cast<char*>(&hk.table_[i][j].fingerprint), sizeof(std::uint32_t));
                in.read(reinterpret_cast<char*>(&hk.table_[i][j].count), sizeof(std::uint32_t));
            }
        }

        std::uint64_t map_size;
        in.read(reinterpret_cast<char*>(&map_size), sizeof(map_size));

        for (std::size_t i = 0; i < map_size; ++i) {
            std::uint64_t len;
            in.read(reinterpret_cast<char*>(&len), sizeof(len));
            std::vector<char> buf(len);
            in.read(buf.data(), static_cast<std::streamsize>(len));
            std::uint64_t cnt;
            in.read(reinterpret_cast<char*>(&cnt), sizeof(cnt));

            T key;
            if constexpr (std::is_same_v<T, std::string>) {
                key = std::string(buf.data(), len);
            } else if constexpr (std::is_same_v<T, std::string_view>) {
                hk.key_storage_.push_back(std::string(buf.data(), len));
                key = hk.key_storage_.back();
            } else {
                std::memcpy(&key, buf.data(), sizeof(T));
            }

            ItemNode* node = new ItemNode{key, nullptr, nullptr, nullptr};
            hk.index_[key] = node;

            FreqBucket* b = hk.find_or_create_bucket(cnt);
            hk.add_item_to_bucket(b, node);
        }

        if (!in) {
            throw std::runtime_error("HeavyKeeper::deserialize: read failed");
        }

        hk.cache_dirty_ = true;
        return hk;
    }

    // =========================================================================
    // Statistics & Introspection
    // =========================================================================

    [[nodiscard]] std::size_t d() const noexcept { return d_; }
    [[nodiscard]] std::size_t w() const noexcept { return w_; }
    [[nodiscard]] std::size_t k() const noexcept { return k_; }

    [[nodiscard]] std::size_t memory_bytes() const noexcept {
        std::size_t total = sizeof(HeavyKeeper);
        total += d_ * sizeof(std::vector<Bucket>) + d_ * w_ * sizeof(Bucket);
        total += index_.bucket_count() * sizeof(void*) + index_.size() * (sizeof(T) + sizeof(void*) + 32);
        for (const auto& str : key_storage_) {
            total += str.capacity();
        }
        FreqBucket* curr_b = bucket_head_;
        while (curr_b) {
            total += sizeof(FreqBucket);
            ItemNode* curr_i = curr_b->item_head;
            while (curr_i) {
                total += sizeof(ItemNode);
                curr_i = curr_i->next;
            }
            curr_b = curr_b->next;
        }
        return total;
    }

    [[nodiscard]] std::size_t memory_usage() const noexcept { return memory_bytes(); }

    const std::vector<Entry>& entries() const noexcept {
        if (cache_dirty_) {
            cached_entries_.clear();
            cached_entries_.reserve(index_.size());
            FreqBucket* curr_b = bucket_head_;
            while (curr_b) {
                ItemNode* curr_i = curr_b->item_head;
                while (curr_i) {
                    cached_entries_.push_back(Entry{curr_i->key, curr_b->count});
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
        for (std::size_t i = 0; i < d_; ++i) {
            std::fill(table_[i].begin(), table_[i].end(), Bucket{0, 0});
        }
    }

private:
    static constexpr std::uint64_t fmix64(std::uint64_t k) noexcept {
        k ^= k >> 33;
        k *= 0xff51afd7ed558ccdULL;
        k ^= k >> 33;
        k *= 0xc4ceb9fe1a85ec53ULL;
        k ^= k >> 33;
        return k;
    }

    static constexpr double const_pow(double base, int exp) {
        double res = 1.0;
        if (exp < 0) {
            base = 1.0 / base;
            exp = -exp;
        }
        for (int i = 0; i < exp; ++i) {
            res *= base;
        }
        return res;
    }

    static constexpr std::array<std::uint32_t, 256> make_decay_lut() {
        std::array<std::uint32_t, 256> arr{};
        double b = 1.08;
        for (int i = 0; i < 256; ++i) {
            double prob = const_pow(b, -i);
            arr[i] = static_cast<std::uint32_t>(prob * 4294967295.0 + 0.5);
        }
        return arr;
    }

    static inline std::uint32_t get_decay_threshold(std::uint32_t count) noexcept {
        static constexpr std::array<std::uint32_t, 256> DECAY_LUT = make_decay_lut();
        if (count >= 256) return 0;
        return DECAY_LUT[count];
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
        FreqBucket* curr_b = bucket_head_;
        while (curr_b) {
            ItemNode* curr_i = curr_b->item_head;
            while (curr_i) {
                ItemNode* next_i = curr_i->next;
                delete curr_i;
                curr_i = next_i;
            }
            FreqBucket* next_b = curr_b->next;
            delete curr_b;
            curr_b = next_b;
        }
        bucket_head_ = nullptr;
        bucket_tail_ = nullptr;
        index_.clear();
        key_storage_.clear();
        cache_dirty_ = true;
    }

    void copy_from(const HeavyKeeper& other) {
        FreqBucket* other_b = other.bucket_head_;
        while (other_b) {
            FreqBucket* new_b = new FreqBucket{other_b->count, nullptr, nullptr, bucket_tail_, nullptr};
            if (bucket_tail_) {
                bucket_tail_->next = new_b;
            } else {
                bucket_head_ = new_b;
            }
            bucket_tail_ = new_b;

            ItemNode* other_i = other_b->item_head;
            while (other_i) {
                T new_key = copy_key(other_i->key);
                ItemNode* new_i = new ItemNode{new_key, new_b, nullptr, nullptr};
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

    FreqBucket* find_or_create_bucket(std::size_t target_count) {
        if (!bucket_head_) {
            FreqBucket* new_bucket = new FreqBucket{target_count, nullptr, nullptr, nullptr, nullptr};
            bucket_head_ = new_bucket;
            bucket_tail_ = new_bucket;
            return new_bucket;
        }
        FreqBucket* curr = bucket_head_;
        while (curr) {
            if (curr->count == target_count) {
                return curr;
            }
            if (curr->count > target_count) {
                FreqBucket* new_bucket = new FreqBucket{target_count, nullptr, nullptr, curr->prev, curr};
                if (curr->prev) {
                    curr->prev->next = new_bucket;
                } else {
                    bucket_head_ = new_bucket;
                }
                curr->prev = new_bucket;
                return new_bucket;
            }
            if (!curr->next) {
                FreqBucket* new_bucket = new FreqBucket{target_count, nullptr, nullptr, curr, nullptr};
                curr->next = new_bucket;
                bucket_tail_ = new_bucket;
                return new_bucket;
            }
            curr = curr->next;
        }
        return nullptr;
    }

    void remove_item_from_bucket(ItemNode* node) {
        FreqBucket* b = node->parent;
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

    void add_item_to_bucket(FreqBucket* b, ItemNode* node) {
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

    std::size_t d_;
    std::size_t w_;
    std::size_t k_;
    Hash hasher_;
    std::vector<std::vector<Bucket>> table_;
    XorShift64 rng_;

    FreqBucket* bucket_head_ = nullptr;
    FreqBucket* bucket_tail_ = nullptr;
    std::unordered_map<T, ItemNode*, Hash> index_;
    std::list<std::string> key_storage_;
    mutable std::vector<Entry> cached_entries_;
    mutable bool cache_dirty_;
};

} // namespace probds
