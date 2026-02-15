#pragma once
// =============================================================================
// quotient_filter.hpp — Quotient Filter
// probDS: High-Performance Probabilistic Data Structures Library
// =============================================================================

#include "hash.hpp"
#include <algorithm>
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
class QuotientFilter {
public:
    static_assert(is_hash_policy_v<Hash, T>, "Hash must satisfy the HashPolicy concept");

    // Packed slot layout: 13-bit remainder + 3 status bits = 16 bits = 2 bytes
    struct Slot {
        std::uint16_t remainder : 13;
        std::uint16_t is_occupied : 1;
        std::uint16_t is_continuation : 1;
        std::uint16_t is_shifted : 1;

        bool is_empty() const noexcept {
            return !is_occupied && !is_continuation && !is_shifted;
        }

        void clear() noexcept {
            remainder = 0;
            is_occupied = 0;
            is_continuation = 0;
            is_shifted = 0;
        }
    };

    static_assert(sizeof(Slot) == 2, "Slot must be exactly 2 bytes");

    // =========================================================================
    // Construction
    // =========================================================================

    explicit QuotientFilter(std::size_t capacity, Hash hasher = Hash{})
        : count_(0), hasher_(std::move(hasher))
    {
        if (capacity == 0) {
            throw std::invalid_argument("QuotientFilter: capacity must be > 0");
        }

        // Quotient bits q: 2^q slots. We aim for a ~75% load factor.
        const std::size_t min_slots = static_cast<std::size_t>(std::ceil(static_cast<double>(capacity) / 0.75));
        q_bits_ = 0;
        std::size_t power = 1;
        while (power < min_slots) {
            power <<= 1;
            ++q_bits_;
        }
        if (q_bits_ < 3) q_bits_ = 3; // Minimum size

        num_slots_ = std::size_t{1} << q_bits_;
        mask_ = num_slots_ - 1;
        table_.resize(num_slots_, Slot{0, 0, 0, 0});
    }

    // =========================================================================
    // Core Operations
    // =========================================================================

    /// Insert a key
    bool insert(const T& key) {
        if (count_ >= num_slots_ * 0.95) {
            return false; // Table too full
        }

        const auto [Q, R] = get_q_r(key);

        if (table_[Q].is_empty()) {
            table_[Q].remainder = R;
            table_[Q].is_occupied = 1;
            ++count_;
            return true;
        }

        const std::size_t s = find_cluster_start(Q);
        const std::size_t num_runs = count_occupied_slots(s, Q);

        std::size_t run_ptr = s;
        std::size_t runs_skipped = 0;
        while (runs_skipped < num_runs) {
            run_ptr = (run_ptr + 1) & mask_;
            while (table_[run_ptr].is_continuation) {
                run_ptr = (run_ptr + 1) & mask_;
            }
            ++runs_skipped;
        }

        std::size_t insert_ptr = run_ptr;
        bool exists = false;
        bool has_run = table_[Q].is_occupied;

        if (has_run) {
            // Check if R already exists in the run
            if (table_[insert_ptr].remainder == R) {
                exists = true;
            }
            std::size_t scan = (insert_ptr + 1) & mask_;
            while (table_[scan].is_continuation) {
                if (table_[scan].remainder == R) {
                    exists = true;
                }
                scan = (scan + 1) & mask_;
            }

            if (exists) {
                return true; // Already exists
            }

            // Append R to the end of the run
            insert_ptr = scan;
        }

        // Find first empty slot
        std::size_t empty_ptr = insert_ptr;
        while (!table_[empty_ptr].is_empty()) {
            empty_ptr = (empty_ptr + 1) & mask_;
        }

        // Shift everything right
        shift_right(insert_ptr, empty_ptr);

        // Place new element
        table_[insert_ptr].remainder = R;
        table_[insert_ptr].is_shifted = (insert_ptr != Q);

        if (has_run) {
            table_[insert_ptr].is_continuation = 1;
        } else {
            table_[insert_ptr].is_continuation = 0;
            table_[Q].is_occupied = 1;
        }

        // Fix continuation bit for shifted runs
        if (has_run && insert_ptr == run_ptr) {
            // If we inserted at the start of the run, the old first element becomes a continuation
            table_[(insert_ptr + 1) & mask_].is_continuation = 1;
            table_[insert_ptr].is_continuation = 0;
        }

        ++count_;
        return true;
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
        const auto [Q, R] = get_q_r(key);

        if (!table_[Q].is_occupied) {
            return false;
        }

        const std::size_t s = find_cluster_start(Q);
        const std::size_t num_runs = count_occupied_slots(s, Q);

        std::size_t run_ptr = s;
        std::size_t runs_skipped = 0;
        while (runs_skipped < num_runs) {
            run_ptr = (run_ptr + 1) & mask_;
            while (table_[run_ptr].is_continuation) {
                run_ptr = (run_ptr + 1) & mask_;
            }
            ++runs_skipped;
        }

        if (table_[run_ptr].remainder == R) {
            return true;
        }

        run_ptr = (run_ptr + 1) & mask_;
        while (table_[run_ptr].is_continuation) {
            if (table_[run_ptr].remainder == R) {
                return true;
            }
            run_ptr = (run_ptr + 1) & mask_;
        }

        return false;
    }

    /// Remove a key
    bool remove(const T& key) {
        const auto [Q, R] = get_q_r(key);

        if (!table_[Q].is_occupied) {
            return false;
        }

        const std::size_t s = find_cluster_start(Q);
        const std::size_t num_runs = count_occupied_slots(s, Q);

        std::size_t run_ptr = s;
        std::size_t runs_skipped = 0;
        while (runs_skipped < num_runs) {
            run_ptr = (run_ptr + 1) & mask_;
            while (table_[run_ptr].is_continuation) {
                run_ptr = (run_ptr + 1) & mask_;
            }
            ++runs_skipped;
        }

        std::size_t delete_ptr = num_slots_;
        if (table_[run_ptr].remainder == R) {
            delete_ptr = run_ptr;
        } else {
            std::size_t scan = (run_ptr + 1) & mask_;
            while (table_[scan].is_continuation) {
                if (table_[scan].remainder == R) {
                    delete_ptr = scan;
                    break;
                }
                scan = (scan + 1) & mask_;
            }
        }

        if (delete_ptr == num_slots_) {
            return false; // Not found
        }

        const bool run_has_more = table_[(delete_ptr + 1) & mask_].is_continuation;
        const bool deleting_first = (delete_ptr == run_ptr);

        // Shift left
        shift_left(delete_ptr);

        // Update occupies flag
        if (deleting_first && !run_has_more) {
            table_[Q].is_occupied = 0;
        }

        if (deleting_first && run_has_more) {
            // The new first element of the run is no longer a continuation
            table_[run_ptr].is_continuation = 0;
        }

        --count_;
        return true;
    }

    void clear() noexcept {
        for (auto& slot : table_) {
            slot.clear();
        }
        count_ = 0;
    }

    // =========================================================================
    // Set Operations (Compatibility required)
    // =========================================================================

    QuotientFilter& operator|=(const QuotientFilter& other) {
        check_compatible(other, "operator|=");
        // Since QuotientFilter is a hash table, union involves inserting other's elements
        // into this filter.
        for (std::size_t i = 0; i < other.num_slots_; ++i) {
            if (other.table_[i].is_occupied) {
                // Find all remainder values for run i
                const std::size_t s = other.find_cluster_start(i);
                const std::size_t num_runs = other.count_occupied_slots(s, i);
                std::size_t run_ptr = s;
                std::size_t runs_skipped = 0;
                while (runs_skipped < num_runs) {
                    run_ptr = (run_ptr + 1) & other.mask_;
                    while (other.table_[run_ptr].is_continuation) {
                        run_ptr = (run_ptr + 1) & other.mask_;
                    }
                    ++runs_skipped;
                }

                // Insert the first remainder
                insert_raw(i, other.table_[run_ptr].remainder);

                run_ptr = (run_ptr + 1) & other.mask_;
                while (other.table_[run_ptr].is_continuation) {
                    insert_raw(i, other.table_[run_ptr].remainder);
                    run_ptr = (run_ptr + 1) & other.mask_;
                }
            }
        }
        return *this;
    }

    QuotientFilter& operator&=(const QuotientFilter& other) {
        check_compatible(other, "operator&=");
        // Approximate intersection: keep only elements present in both.
        // We can build a temporary filter and then swap.
        QuotientFilter temp(num_slots_ * 3 / 4, hasher_);

        for (std::size_t i = 0; i < num_slots_; ++i) {
            if (table_[i].is_occupied) {
                const std::size_t s = find_cluster_start(i);
                const std::size_t num_runs = count_occupied_slots(s, i);
                std::size_t run_ptr = s;
                std::size_t runs_skipped = 0;
                while (runs_skipped < num_runs) {
                    run_ptr = (run_ptr + 1) & mask_;
                    while (table_[run_ptr].is_continuation) {
                        run_ptr = (run_ptr + 1) & mask_;
                    }
                    ++runs_skipped;
                }

                // Check and insert first
                std::uint16_t R = table_[run_ptr].remainder;
                if (other.contains_raw(i, R)) {
                    temp.insert_raw(i, R);
                }

                run_ptr = (run_ptr + 1) & mask_;
                while (table_[run_ptr].is_continuation) {
                    R = table_[run_ptr].remainder;
                    if (other.contains_raw(i, R)) {
                        temp.insert_raw(i, R);
                    }
                    run_ptr = (run_ptr + 1) & mask_;
                }
            }
        }
        std::swap(table_, temp.table_);
        std::swap(count_, temp.count_);
        return *this;
    }

    friend QuotientFilter operator|(QuotientFilter lhs, const QuotientFilter& rhs) {
        lhs |= rhs;
        return lhs;
    }

    friend QuotientFilter operator&(QuotientFilter lhs, const QuotientFilter& rhs) {
        lhs &= rhs;
        return lhs;
    }

    // =========================================================================
    // Statistics & Introspection
    // =========================================================================

    [[nodiscard]] std::size_t size() const noexcept { return count_; }
    [[nodiscard]] std::size_t capacity() const noexcept { return num_slots_; }
    [[nodiscard]] std::size_t memory_bytes() const noexcept { return num_slots_ * sizeof(Slot); }
    [[nodiscard]] std::size_t memory_usage() const noexcept { return memory_bytes(); }

    [[nodiscard]] double load_factor() const noexcept {
        return static_cast<double>(count_) / static_cast<double>(num_slots_);
    }

    // =========================================================================
    // Serialization
    // =========================================================================

    void serialize(std::ostream& out) const {
        out.write("PQF2", 4);
        write_u64(out, q_bits_);
        write_u64(out, num_slots_);
        write_u64(out, count_);
        out.write(reinterpret_cast<const char*>(table_.data()), static_cast<std::streamsize>(num_slots_ * sizeof(Slot)));
        if (!out) {
            throw std::runtime_error("QuotientFilter::serialize: write failed");
        }
    }

    static QuotientFilter deserialize(std::istream& in, Hash hasher = Hash{}) {
        char magic[4];
        in.read(magic, 4);
        if (std::memcmp(magic, "PQF2", 4) != 0) {
            throw std::runtime_error("QuotientFilter::deserialize: invalid magic number");
        }

        const auto q_bits = read_u64(in);
        const auto num_slots = read_u64(in);
        const auto count = read_u64(in);

        QuotientFilter filter(num_slots, q_bits, count, std::move(hasher));
        in.read(reinterpret_cast<char*>(filter.table_.data()), static_cast<std::streamsize>(num_slots * sizeof(Slot)));

        if (!in) {
            throw std::runtime_error("QuotientFilter::deserialize: read failed");
        }
        return filter;
    }

private:
    QuotientFilter(std::size_t num_slots, std::size_t q_bits, std::size_t count, Hash hasher)
        : count_(count), q_bits_(q_bits), num_slots_(num_slots), mask_(num_slots - 1), hasher_(std::move(hasher))
    {
        table_.resize(num_slots_, Slot{0, 0, 0, 0});
    }

    [[nodiscard]] std::pair<std::size_t, std::uint16_t> get_q_r(const T& key) const noexcept {
        const std::uint64_t hash = hasher_(key);
        const std::size_t Q = hash & mask_;
        const std::uint16_t R = static_cast<std::uint16_t>((hash >> q_bits_) & 0x1FFF); // 13 bits
        return {Q, R};
    }

    [[nodiscard]] std::size_t find_cluster_start(std::size_t Q) const noexcept {
        std::size_t s = Q;
        while (table_[s].is_shifted) {
            s = (s - 1) & mask_;
        }
        return s;
    }

    [[nodiscard]] std::size_t count_occupied_slots(std::size_t start, std::size_t end) const noexcept {
        std::size_t count = 0;
        std::size_t curr = start;
        while (curr != end) {
            if (table_[curr].is_occupied) {
                ++count;
            }
            curr = (curr + 1) & mask_;
        }
        return count;
    }

    void shift_right(std::size_t start, std::size_t end) noexcept {
        std::size_t curr = end;
        while (curr != start) {
            std::size_t prev = (curr - 1) & mask_;
            table_[curr].remainder = table_[prev].remainder;
            table_[curr].is_shifted = 1;
            table_[curr].is_continuation = table_[prev].is_continuation;
            curr = prev;
        }
    }

    void shift_left(std::size_t start) noexcept {
        std::size_t curr = start;
        std::size_t next = (curr + 1) & mask_;

        while (table_[next].is_shifted && !table_[next].is_empty()) {
            table_[curr].remainder = table_[next].remainder;
            table_[curr].is_shifted = table_[next].is_shifted;
            table_[curr].is_continuation = table_[next].is_continuation;
            curr = next;
            next = (curr + 1) & mask_;
        }
        table_[curr].clear();
    }

    bool insert_raw(std::size_t Q, std::uint16_t R) {
        if (table_[Q].is_empty()) {
            table_[Q].remainder = R;
            table_[Q].is_occupied = 1;
            ++count_;
            return true;
        }

        const std::size_t s = find_cluster_start(Q);
        const std::size_t num_runs = count_occupied_slots(s, Q);

        std::size_t run_ptr = s;
        std::size_t runs_skipped = 0;
        while (runs_skipped < num_runs) {
            run_ptr = (run_ptr + 1) & mask_;
            while (table_[run_ptr].is_continuation) {
                run_ptr = (run_ptr + 1) & mask_;
            }
            ++runs_skipped;
        }

        std::size_t insert_ptr = run_ptr;
        bool has_run = table_[Q].is_occupied;
        if (has_run) {
            std::size_t scan = (insert_ptr + 1) & mask_;
            while (table_[scan].is_continuation) {
                scan = (scan + 1) & mask_;
            }
            insert_ptr = scan;
        }

        std::size_t empty_ptr = insert_ptr;
        while (!table_[empty_ptr].is_empty()) {
            empty_ptr = (empty_ptr + 1) & mask_;
        }

        shift_right(insert_ptr, empty_ptr);

        table_[insert_ptr].remainder = R;
        table_[insert_ptr].is_shifted = (insert_ptr != Q);

        if (has_run) {
            table_[insert_ptr].is_continuation = 1;
        } else {
            table_[insert_ptr].is_continuation = 0;
            table_[Q].is_occupied = 1;
        }

        if (has_run && insert_ptr == run_ptr) {
            table_[(insert_ptr + 1) & mask_].is_continuation = 1;
            table_[insert_ptr].is_continuation = 0;
        }

        ++count_;
        return true;
    }

    [[nodiscard]] bool contains_raw(std::size_t Q, std::uint16_t R) const noexcept {
        if (!table_[Q].is_occupied) return false;
        const std::size_t s = find_cluster_start(Q);
        const std::size_t num_runs = count_occupied_slots(s, Q);
        std::size_t run_ptr = s;
        std::size_t runs_skipped = 0;
        while (runs_skipped < num_runs) {
            run_ptr = (run_ptr + 1) & mask_;
            while (table_[run_ptr].is_continuation) {
                run_ptr = (run_ptr + 1) & mask_;
            }
            ++runs_skipped;
        }
        if (table_[run_ptr].remainder == R) return true;
        run_ptr = (run_ptr + 1) & mask_;
        while (table_[run_ptr].is_continuation) {
            if (table_[run_ptr].remainder == R) return true;
            run_ptr = (run_ptr + 1) & mask_;
        }
        return false;
    }

    void check_compatible(const QuotientFilter& other, const char* op) const {
        if (num_slots_ != other.num_slots_ || q_bits_ != other.q_bits_) {
            throw std::invalid_argument(
                std::string("QuotientFilter::") + op + ": filters are incompatible");
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

    std::size_t count_;
    std::size_t q_bits_;
    std::size_t num_slots_;
    std::size_t mask_;
    Hash hasher_;
    std::vector<Slot> table_;
};

} // namespace probds
