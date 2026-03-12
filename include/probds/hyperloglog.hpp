#pragma once
// =============================================================================
// hyperloglog.hpp — HyperLogLog Cardinality Estimator
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
#include <string>
#include <utility>
#include <vector>

#if defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace probds {

template <typename T = std::string_view, typename Hash = DefaultHash<T>>
class HyperLogLog {
public:
    static_assert(is_hash_policy_v<Hash, T>, "Hash must satisfy the HashPolicy concept");

    // =========================================================================
    // Construction
    // =========================================================================

    explicit HyperLogLog(std::uint8_t precision = 14, Hash hasher = Hash{})
        : precision_(precision), hasher_(std::move(hasher))
    {
        if (precision < 4 || precision > 16) {
            throw std::invalid_argument(
                "HyperLogLog: precision must be in [4, 16], got " + std::to_string(precision));
        }

        const std::size_t m = std::size_t{1} << precision;
        registers_.resize(m, 0);

        for (int i = 0; i <= 64; ++i) {
            pow2_table_[i] = 1.0 / static_cast<double>(1ULL << i);
        }
        // Note: 1ULL << 64 is UB, handle separately:
        pow2_table_[64] = std::ldexp(1.0, -64);

        current_harmonic_sum_ = static_cast<double>(m);
        zero_registers_ = static_cast<std::uint32_t>(m);
    }

    // =========================================================================
    // Core Operations
    // =========================================================================

    /// Insert a key
    void insert(const T& key) {
        const std::uint64_t hash = hasher_(key);

        // Extract register index j from the top p bits
        const std::size_t j = hash >> (64 - precision_);

        // Remaining bits: count leading zeros
        const std::uint64_t remaining = (hash << precision_) | std::uint64_t{1};
        const std::uint8_t rho = static_cast<std::uint8_t>(__builtin_clzll(remaining) + 1);

        if (rho > registers_[j]) {
            const std::uint8_t old_zeros = registers_[j];
            const std::uint8_t new_zeros = rho;

            current_harmonic_sum_ -= pow2_table_[old_zeros];
            current_harmonic_sum_ += pow2_table_[new_zeros];
            if (old_zeros == 0) {
                --zero_registers_;
            }
            registers_[j] = rho;
        }
    }

    /// Bulk insert
    template <typename InputIt>
    void insert(InputIt first, InputIt last) {
        for (; first != last; ++first) {
            insert(*first);
        }
    }

    /// Estimate cardinality
    [[nodiscard]] std::uint64_t estimate() const {
        const double m_dbl = static_cast<double>(registers_.size());
        const double alpha = alpha_m(registers_.size());
        double E = alpha * m_dbl * m_dbl / current_harmonic_sum_;
        if (E <= 2.5 * m_dbl && zero_registers_ > 0) {
            E = m_dbl * std::log(m_dbl / static_cast<double>(zero_registers_));
        }
        return static_cast<std::uint64_t>(E + 0.5);
    }

    /// Merge another HyperLogLog sketch into this one
    void merge(const HyperLogLog& other) {
        if (precision_ != other.precision_) {
            throw std::invalid_argument(
                "HyperLogLog::merge: precision mismatch (" + std::to_string(precision_) +
                " vs " + std::to_string(other.precision_) + ")");
        }

        std::size_t j = 0;
        const std::size_t size = registers_.size();

#if defined(__aarch64__)
        for (; j + 63 < size; j += 64) {
            uint8x16_t t0 = vld1q_u8(&registers_[j]);
            uint8x16_t o0 = vld1q_u8(&other.registers_[j]);
            uint8x16_t t1 = vld1q_u8(&registers_[j + 16]);
            uint8x16_t o1 = vld1q_u8(&other.registers_[j + 16]);
            uint8x16_t t2 = vld1q_u8(&registers_[j + 32]);
            uint8x16_t o2 = vld1q_u8(&other.registers_[j + 32]);
            uint8x16_t t3 = vld1q_u8(&registers_[j + 48]);
            uint8x16_t o3 = vld1q_u8(&other.registers_[j + 48]);

            vst1q_u8(&registers_[j], vmaxq_u8(t0, o0));
            vst1q_u8(&registers_[j + 16], vmaxq_u8(t1, o1));
            vst1q_u8(&registers_[j + 32], vmaxq_u8(t2, o2));
            vst1q_u8(&registers_[j + 48], vmaxq_u8(t3, o3));
        }
        for (; j + 15 < size; j += 16) {
            uint8x16_t v_this = vld1q_u8(&registers_[j]);
            uint8x16_t v_other = vld1q_u8(&other.registers_[j]);
            vst1q_u8(&registers_[j], vmaxq_u8(v_this, v_other));
        }
#endif

        for (; j < size; ++j) {
            registers_[j] = std::max(registers_[j], other.registers_[j]);
        }

        // Recompute zero count using NEON
        std::uint32_t zeros = 0;
        std::size_t i = 0;
#if defined(__aarch64__)
        for (; i + 63 < size; i += 64) {
            uint8x16_t v0 = vld1q_u8(&registers_[i]);
            uint8x16_t v1 = vld1q_u8(&registers_[i + 16]);
            uint8x16_t v2 = vld1q_u8(&registers_[i + 32]);
            uint8x16_t v3 = vld1q_u8(&registers_[i + 48]);

            uint8x16_t z0 = vceqzq_u8(v0);
            uint8x16_t z1 = vceqzq_u8(v1);
            uint8x16_t z2 = vceqzq_u8(v2);
            uint8x16_t z3 = vceqzq_u8(v3);

            uint8x16_t s0 = vshrq_n_u8(z0, 7);
            uint8x16_t s1 = vshrq_n_u8(z1, 7);
            uint8x16_t s2 = vshrq_n_u8(z2, 7);
            uint8x16_t s3 = vshrq_n_u8(z3, 7);

            zeros += vaddvq_u8(s0);
            zeros += vaddvq_u8(s1);
            zeros += vaddvq_u8(s2);
            zeros += vaddvq_u8(s3);
        }
        for (; i + 15 < size; i += 16) {
            uint8x16_t v = vld1q_u8(&registers_[i]);
            uint8x16_t v_zero = vceqzq_u8(v);
            uint8x16_t v_is_zero = vshrq_n_u8(v_zero, 7);
            zeros += vaddvq_u8(v_is_zero);
        }
#endif
        for (; i < size; ++i) {
            zeros += (registers_[i] == 0);
        }
        zero_registers_ = zeros;

        // Recompute harmonic sum in an unrolled loop
        double sum0 = 0.0;
        double sum1 = 0.0;
        double sum2 = 0.0;
        double sum3 = 0.0;
        double sum4 = 0.0;
        double sum5 = 0.0;
        double sum6 = 0.0;
        double sum7 = 0.0;
        std::size_t k = 0;
        for (; k + 7 < size; k += 8) {
            sum0 += pow2_table_[registers_[k]];
            sum1 += pow2_table_[registers_[k + 1]];
            sum2 += pow2_table_[registers_[k + 2]];
            sum3 += pow2_table_[registers_[k + 3]];
            sum4 += pow2_table_[registers_[k + 4]];
            sum5 += pow2_table_[registers_[k + 5]];
            sum6 += pow2_table_[registers_[k + 6]];
            sum7 += pow2_table_[registers_[k + 7]];
        }
        for (; k < size; ++k) {
            sum0 += pow2_table_[registers_[k]];
        }
        current_harmonic_sum_ = (sum0 + sum1 + sum2 + sum3) + (sum4 + sum5 + sum6 + sum7);
    }

    void clear() noexcept {
        std::fill(registers_.begin(), registers_.end(), std::uint8_t{0});
        current_harmonic_sum_ = static_cast<double>(registers_.size());
        zero_registers_ = static_cast<std::uint32_t>(registers_.size());
    }

    // =========================================================================
    // Statistics & Introspection
    // =========================================================================

    [[nodiscard]] double relative_error() const noexcept {
        const double m = static_cast<double>(registers_.size());
        return 1.04 / std::sqrt(m);
    }

    [[nodiscard]] std::size_t memory_bytes() const noexcept {
        return registers_.size() * sizeof(std::uint8_t);
    }
    [[nodiscard]] std::size_t memory_usage() const noexcept { return memory_bytes(); }

    [[nodiscard]] std::uint8_t precision() const noexcept {
        return precision_;
    }

    // =========================================================================
    // Serialization
    // =========================================================================

    void serialize(std::ostream& out) const {
        out.write("PHL2", 4);
        out.write(reinterpret_cast<const char*>(&precision_), 1);
        const std::size_t m = registers_.size();
        out.write(reinterpret_cast<const char*>(registers_.data()), static_cast<std::streamsize>(m));
        if (!out) {
            throw std::runtime_error("HyperLogLog::serialize: write failed");
        }
    }

    static HyperLogLog deserialize(std::istream& in, Hash hasher = Hash{}) {
        char magic[4];
        in.read(magic, 4);
        if (std::memcmp(magic, "PHL2", 4) != 0) {
            throw std::runtime_error("HyperLogLog::deserialize: invalid magic number");
        }
        std::uint8_t precision;
        in.read(reinterpret_cast<char*>(&precision), 1);

        HyperLogLog hll(precision, std::move(hasher));
        const std::size_t m = std::size_t{1} << precision;
        in.read(reinterpret_cast<char*>(hll.registers_.data()), static_cast<std::streamsize>(m));

        if (!in) {
            throw std::runtime_error("HyperLogLog::deserialize: read failed");
        }

        hll.current_harmonic_sum_ = 0.0;
        hll.zero_registers_ = 0;
        for (std::size_t i = 0; i < m; ++i) {
            const std::uint8_t reg = hll.registers_[i];
            hll.current_harmonic_sum_ += hll.pow2_table_[reg];
            hll.zero_registers_ += (reg == 0);
        }

        return hll;
    }

private:
    static double alpha_m(std::size_t m) noexcept {
        switch (m) {
            case 16:  return 0.673;
            case 32:  return 0.697;
            case 64:  return 0.709;
            default:
                return 0.7213 / (1.0 + 1.079 / static_cast<double>(m));
        }
    }

    std::vector<std::uint8_t> registers_;
    std::uint8_t precision_;
    Hash hasher_;
    double pow2_table_[65];
    double current_harmonic_sum_;
    std::uint32_t zero_registers_;
};

} // namespace probds
