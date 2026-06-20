// simd_core_kernels.cpp
//
// Core SIMD-namespace utilities that don't depend on Highway.
// These live in chronon3d_core_impl so they are available to all
// consumers of the core library, avoiding link-order issues
// between chronon3d_core_impl and chronon3d_backend_software.
//
// The SIMD-accelerated kernels (Highway) remain in
// src/backends/software/simd/highway_*_kernels.cpp.

#include <chronon3d/simd/kernels.hpp>
#include <cstring>

namespace chronon3d::simd {

void clear_framebuffer(std::span<Color> data, const Color& color) {
    if (data.empty()) return;
    const int pixel_count = static_cast<int>(data.size());
    // Zero-fill via memset is ~4-8× faster than std::fill (which writes
    // one Color at a time) because the CPU's write-combining and ERMSB
    // rep stosb microcode handles large memset in ~1 cycle per 16 bytes.
    if (color.r == 0.0f && color.g == 0.0f && color.b == 0.0f && color.a == 0.0f) {
        std::memset(static_cast<void*>(data.data()), 0,
                     static_cast<size_t>(pixel_count) * sizeof(Color));
        return;
    }
    std::fill(data.data(), data.data() + pixel_count, color);
}

}  // namespace chronon3d::simd
