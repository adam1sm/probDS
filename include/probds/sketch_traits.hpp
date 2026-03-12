#pragma once
// =============================================================================
// sketch_traits.hpp — Sketch traits for probDS
// =============================================================================

#include <type_traits>
#include <iostream>

namespace probds {

// Enforce the Sketch C++17 concept via type traits.
// A type S is a Sketch for key type T if it compiles:
// - s.insert(std::declval<const T&>())
// - s.memory_bytes() returning size_t
// - s.clear() returning void
// - s.serialize(std::declval<std::ostream&>())

template <typename S, typename T, typename = void>
struct is_sketch : std::false_type {};

template <typename S, typename T>
struct is_sketch<S, T, std::void_t<
    decltype(std::declval<S&>().insert(std::declval<const T&>())),
    decltype(std::declval<S&>().memory_bytes()),
    decltype(std::declval<S&>().clear()),
    decltype(std::declval<const S&>().serialize(std::declval<std::ostream&>()))
>> {
    static constexpr bool value = std::is_same_v<decltype(std::declval<S&>().memory_bytes()), std::size_t>;
};

template <typename S, typename T>
constexpr bool is_sketch_v = is_sketch<S, T>::value;

} // namespace probds
