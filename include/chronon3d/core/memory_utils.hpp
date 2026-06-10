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
    // __builtin_prefetch requires BOTH the second (read=0/write=1) and
    // third (locality) arguments to be compile-time constants.  Route
    // through nested switches so the compiler always sees constants.
    if (write) {
        switch (locality) {
            case 0: __builtin_prefetch(addr, 1, 0); break;
            case 1: __builtin_prefetch(addr, 1, 1); break;
            case 2: __builtin_prefetch(addr, 1, 2); break;
            default: __builtin_prefetch(addr, 1, 3); break;
        }
    } else {
        switch (locality) {
            case 0: __builtin_prefetch(addr, 0, 0); break;
            case 1: __builtin_prefetch(addr, 0, 1); break;
            case 2: __builtin_prefetch(addr, 0, 2); break;
            default: __builtin_prefetch(addr, 0, 3); break;
        }
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
