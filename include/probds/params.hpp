#pragma once
// =============================================================================
// params.hpp — Parameter Estimator and Recommendation Engine
// probDS: High-Performance Probabilistic Data Structures Library
// =============================================================================

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace probds {

// =============================================================================
// Parameter Estimator Structs
// =============================================================================

struct BloomParams {
    std::size_t m;
    std::size_t k;
    std::size_t memory_bytes;
};

struct CuckooParams {
    std::size_t capacity;
    std::size_t bucket_size;
    std::size_t fingerprint_bits;
    std::size_t memory_bytes;
};

struct HllParams {
    std::uint8_t precision;
    std::size_t memory_bytes;
};

struct CmsParams {
    double epsilon;
    double delta;
    std::size_t width;
    std::size_t depth;
    std::size_t memory_bytes;
};

struct Recommendation {
    std::string structure_name;
    std::size_t memory_bytes;
    double expected_fpr;
};

// =============================================================================
// Helper Math Functions
// =============================================================================

namespace detail {
    inline std::size_t next_power_of_two(std::size_t n) noexcept {
        if (n == 0) return 1;
        --n;
        n |= n >> 1;
        n |= n >> 2;
        n |= n >> 4;
        n |= n >> 8;
        n |= n >> 16;
        n |= n >> 32;
        return n + 1;
    }
} // namespace detail

// =============================================================================
// Parameter Estimators
// =============================================================================

inline BloomParams bloom_params(std::size_t n, double fpr) {
    const double ln2 = std::log(2.0);
    const double m_opt = -static_cast<double>(n) * std::log(fpr) / (ln2 * ln2);
    std::size_t m = detail::next_power_of_two(m_opt < 64 ? 64 : static_cast<std::size_t>(std::ceil(m_opt)));
    std::size_t k = static_cast<std::size_t>(std::round(static_cast<double>(m) / static_cast<double>(n) * ln2));
    if (k == 0) k = 1;
    return {m, k, m / 8};
}

inline CuckooParams cuckoo_params(std::size_t n, double fpr) {
    double f_opt = std::ceil(std::log2(8.0 / fpr));
    std::size_t f = static_cast<std::size_t>(f_opt);
    if (f < 1) f = 1;
    if (f > 8) f = 8; // CuckooFilter fingerprint bits are clamped to [1, 8]

    std::size_t bucket_size = 4;
    std::size_t min_buckets = (n + bucket_size - 1) / bucket_size;
    std::size_t num_buckets = detail::next_power_of_two(min_buckets < 2 ? 2 : min_buckets);
    std::size_t capacity = num_buckets * bucket_size;
    std::size_t memory_bytes = num_buckets * bucket_size * sizeof(std::uint8_t);
    return {capacity, bucket_size, f, memory_bytes};
}

inline HllParams hll_params(double rel_error) {
    double p_opt = 2.0 * std::log2(1.04 / rel_error);
    std::size_t p = static_cast<std::size_t>(std::ceil(p_opt));
    if (p < 4) p = 4;
    if (p > 16) p = 16;
    std::size_t m = std::size_t{1} << p;
    return {static_cast<std::uint8_t>(p), m};
}

inline CmsParams cms_params(double epsilon, double delta) {
    const double e = std::exp(1.0);
    std::size_t base_width = static_cast<std::size_t>(std::ceil(e / epsilon));
    std::size_t width = detail::next_power_of_two(base_width < 2 ? 2 : base_width);
    std::size_t depth = static_cast<std::size_t>(std::ceil(std::log(1.0 / delta)));
    if (depth == 0) depth = 1;
    std::size_t memory_bytes = depth * width * sizeof(std::uint64_t);
    return {epsilon, delta, width, depth, memory_bytes};
}

// =============================================================================
// Recommendation Engine
// =============================================================================

inline Recommendation recommend(std::size_t n, double fpr, std::size_t budget_bytes) {
    // 1. Estimate Bloom Filter
    auto bp = bloom_params(n, fpr);
    double bloom_fpr = fpr;

    // 2. Estimate Cuckoo Filter
    auto cp = cuckoo_params(n, fpr);
    double cuckoo_fpr = 8.0 / static_cast<double>(1ULL << cp.fingerprint_bits);

    // 3. Estimate XorFilter (8-bit)
    // Size required: 3 * next_power_of_two(0.41 * n) bytes
    std::size_t xor_block_len = detail::next_power_of_two(static_cast<std::size_t>(std::ceil(0.41 * static_cast<double>(n))));
    if (xor_block_len < 32) xor_block_len = 32; // xor_filter enforces minimum size
    std::size_t xor_memory = 3 * xor_block_len;
    double xor_fpr = 1.0 / 256.0;

    // 4. Estimate RibbonFilter (8-bit)
    // Size required: next_power_of_two(1.08 * n) bytes
    std::size_t ribbon_slots = detail::next_power_of_two(static_cast<std::size_t>(std::ceil(1.08 * static_cast<double>(n))));
    std::size_t ribbon_memory = ribbon_slots;
    double ribbon_fpr = 1.0 / 256.0;

    // We collect all candidates that fit the budget
    struct Candidate {
        std::string name;
        std::size_t memory;
        double achieved_fpr;
    };

    std::vector<Candidate> candidates;
    if (bp.memory_bytes <= budget_bytes) candidates.push_back({"BloomFilter", bp.memory_bytes, bloom_fpr});
    if (cp.memory_bytes <= budget_bytes) candidates.push_back({"CuckooFilter", cp.memory_bytes, cuckoo_fpr});
    if (xor_memory <= budget_bytes) candidates.push_back({"XorFilter", xor_memory, xor_fpr});
    if (ribbon_memory <= budget_bytes) candidates.push_back({"RibbonFilter", ribbon_memory, ribbon_fpr});

    if (candidates.empty()) {
        // If nothing fits within the budget, recommend the one that uses the least memory
        Candidate best = {"BloomFilter", bp.memory_bytes, bloom_fpr};
        if (cp.memory_bytes < best.memory) best = {"CuckooFilter", cp.memory_bytes, cuckoo_fpr};
        if (xor_memory < best.memory) best = {"XorFilter", xor_memory, xor_fpr};
        if (ribbon_memory < best.memory) best = {"RibbonFilter", ribbon_memory, ribbon_fpr};
        return {best.name, best.memory, best.achieved_fpr};
    }

    // Otherwise, choose the candidate that achieves the lowest FPR
    auto best_it = std::min_element(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (std::abs(a.achieved_fpr - b.achieved_fpr) > 1e-9) {
            return a.achieved_fpr < b.achieved_fpr;
        }
        return a.memory < b.memory; // Tie-breaker: less memory
    });

    return {best_it->name, best_it->memory, best_it->achieved_fpr};
}

} // namespace probds
