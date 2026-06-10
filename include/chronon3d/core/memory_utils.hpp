#pragma once

#include <cstddef>

#if defined(_MSC_VER)
#include <intrin.h>
#include <mmintrin.h>
#include <xmmintrin.h>
#endif

namespace chronon3d {

/**
 * @brief Portable prefetch hint.
 * 
 * @param addr Memory address to prefetch.
 * @param write True if the prefetch is for a write operation, false for read.
 * @param locality 0-3, where 0 is least local and 3 is most local (kept in cache).
 */
inline void prefetch(const void* addr, bool write = false, int locality = 3) {
#if defined(__GNUC__) || defined(__clang__)
    if (write) {
        __builtin_prefetch(addr, 1, locality);
    } else {
        __builtin_prefetch(addr, 0, locality);
    }
#elif defined(_MSC_VER)
    #if defined(_M_X64) || defined(_M_IX86)
        _mm_prefetch(static_cast<const char*>(addr), _MM_HINT_T0);
    #endif
    (void)write;
    (void)locality;
#else
    (void)addr;
    (void)write;
    (void)locality;
#endif
}

} // namespace chronon3d
