#pragma once
// =============================================================================
// hash.hpp — Hash utilities for probDS
// =============================================================================

#include <cstdint>
#include <string_view>
#include <type_traits>

// Enable inline implementation of all xxhash functions
#ifndef XXH_INLINE_ALL
#define XXH_INLINE_ALL
#endif
#include "xxhash.h"
#include "wyhash.h"

namespace probds {

// =============================================================================
// HashPolicy Concept validation
// =============================================================================
template <typename H, typename T, typename = void>
struct is_hash_policy : std::false_type {};

template <typename H, typename T>
struct is_hash_policy<H, T, std::void_t<
    decltype(std::declval<const H&>()(std::declval<const T&>()))
>> : std::is_same<decltype(std::declval<const H&>()(std::declval<const T&>())), uint64_t> {};

template <typename H, typename T>
constexpr bool is_hash_policy_v = is_hash_policy<H, T>::value;

// =============================================================================
// Helper to hash arbitrary bytes
// =============================================================================
namespace detail {

template <typename T>
std::string_view to_string_view(const T& key) noexcept {
    if constexpr (std::is_convertible_v<const T&, std::string_view>) {
        return std::string_view(key);
    } else {
        return std::string_view(reinterpret_cast<const char*>(&key), sizeof(T));
    }
}

} // namespace detail

// =============================================================================
// WyHash Wrapper
// =============================================================================
template <typename T>
struct WyHash {
    uint64_t operator()(const T& key) const noexcept {
        auto sv = detail::to_string_view(key);
        // Using Wang Yi's wyhash with seed 0 and standard secret
        return ::wyhash(sv.data(), sv.size(), 0ULL, _wyp);
    }
};

// =============================================================================
// XXH3 Wrapper
// =============================================================================
template <typename T>
struct XXH3 {
    uint64_t operator()(const T& key) const noexcept {
        auto sv = detail::to_string_view(key);
        // Using XXH3 64-bit variant
        return ::XXH3_64bits(sv.data(), sv.size());
    }
};

// =============================================================================
// Default Hash helper (using WyHash as default)
// =============================================================================
template <typename T>
using DefaultHash = WyHash<T>;

} // namespace probds
