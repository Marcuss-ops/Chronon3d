#pragma once

#include <chronon3d/core/types.hpp>
#include <string_view>

namespace chronon3d {

/**
 * Compile-time FNV-1a hash implementation.
 */
struct Hash {
    static constexpr u64 offset_basis = 0xcbf29ce484222325ULL;
    static constexpr u64 prime = 0x100000001b3ULL;

    static constexpr u64 fnv1a(std::string_view s) {
        u64 hash = offset_basis;
        for (char c : s) {
            hash ^= static_cast<u64>(c);
            hash *= prime;
        }
        return hash;
    }
};

/**
 * User-defined literal for compile-time hashing.
 * Example: "MyTitle"_id
 */
constexpr u64 operator""_id(const char* s, std::size_t n) {
    return Hash::fnv1a(std::string_view(s, n));
}

} // namespace chronon3d
