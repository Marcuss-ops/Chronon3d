#pragma once

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <cstdint>
#include <cstddef>

#if defined(_WIN32)
#undef min
#undef max
#endif

namespace chronon3d {

// Floating point types
using f32 = float;
using f64 = double;

// Unsigned integer types
using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

// Signed integer types
using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

// Size types
using usize = std::size_t;

#if defined(_MSC_VER) && !defined(__restrict__)
#define __restrict__ __restrict
#endif

} // namespace chronon3d
