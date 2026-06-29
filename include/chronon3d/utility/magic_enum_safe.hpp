#pragma once

// magic_enum_safe.hpp
//
// Wrapper around magic_enum::containers that turns a std::abort() (which
// is the default in -fno-exceptions / no-MAGIC_ENUM_NO_EXCEPTION builds)
// into a std::optional<T> / std::nullopt return value.
//
// Why this file exists:
//   magic_enum's containers::array / bitset / set call
//   MAGIC_ENUM_THROW(...) which under
//     - exceptions enabled + MAGIC_ENUM_NO_EXCEPTION undefined  -> throw
//     - exceptions disabled / no-MAGIC_ENUM_NO_EXCEPTION          -> std::abort()
//   The abort() is unsafe in a library: it tears down the whole
//   process even when the caller just asked "is this enum value
//   in the container?".  This wrapper catches the abort path by
//   trying the operation under a try/catch (when exceptions are
//   enabled) or by using a "look it up first, never call the
//   throwing accessor" strategy (when exceptions are disabled).
//
// Usage:
//   #include <chronon3d/utility/magic_enum_safe.hpp>
//   using namespace chronon3d::magic_enum_safe;
//
//   magic_enum::containers::array<MyEnum, int> a = ...;
//   std::optional<int> v = safe_at(a, MyEnum::Foo);  // nullopt on miss

#include <magic_enum/magic_enum_containers.hpp>

#include <optional>
#include <type_traits>

#if !defined(MAGIC_ENUM_NO_EXCEPTION) && (defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND))
//  define CHRONON3D_MAGIC_ENUM_EXCEPTIONS_ENABLED 1
#else
//  define CHRONON3D_MAGIC_ENUM_EXCEPTIONS_ENABLED 0
#endif

namespace chronon3d::magic_enum_safe {

// safe_at(array<E, V>, E) -> std::optional<V>
//   Returns the value associated with `e` in the array, or std::nullopt
//   if the enum value is out of range (which is what the throwing
//   `at()` would otherwise abort on).
template <typename E, typename V, typename Index>
inline std::optional<V> safe_at(const magic_enum::containers::array<E, V, Index>& a, E e) noexcept {
#if CHRONON3D_MAGIC_ENUM_EXCEPTIONS_ENABLED
    try {
        return a.at(e);
    } catch (...) {
        return std::nullopt;
    }
#else
    // No exceptions: the public at() will abort.  Use index_type::at
    // (which returns optional<size_t>) to test before dereferencing.
    if (auto i = Index::at(e)) {
        return a.a[*i];
    }
    return std::nullopt;
#endif
}

// safe_at(bitset<E>, E) -> std::optional<bool>
template <typename E, typename Index>
inline std::optional<bool> safe_test(const magic_enum::containers::bitset<E, Index>& b, E e) noexcept {
#if CHRONON3D_MAGIC_ENUM_EXCEPTIONS_ENABLED
    try {
        return b.test(e);
    } catch (...) {
        return std::nullopt;
    }
#else
    if (auto i = Index::at(e)) {
        return static_cast<bool>(typename magic_enum::containers::bitset<E, Index>::const_reference(&b, *i));
    }
    return std::nullopt;
#endif
}

// safe_contains(set<E>, E) -> bool  (true if present OR if out of range;
//                                     this matches the std::set contract
//                                     for `count` which is 0/1)
template <typename E, typename Cmp>
inline bool safe_contains(const magic_enum::containers::set<E, Cmp>& s, E e) noexcept {
    return s.contains(e);
}

} // namespace chronon3d::magic_enum_safe
