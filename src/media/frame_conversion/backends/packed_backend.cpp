// ============================================================================
// packed_backend.cpp — Float→RGBA8 direct quantization (TBB-parallel).
//
// PR5: Moved from frame_converter.cpp.  The swscale backend also calls this
// function to produce the RGBA8 staging buffer before sws_scale().
// ============================================================================

#include <chronon3d/media/frame_conversion/backends/packed_backend.hpp>

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/core/parallel_tracked.hpp>

#include <tbb/parallel_for.h>
#include <algorithm>
#include <cstdint>

namespace chronon3d::video::packed {

void convert_fb_to_rgba8(const Framebuffer& src,
                         int width, int height,
                         bool apply_gamma,
                         uint8_t* rgba8)
{
    const Color* src_data = src.data();
    const int alloc_w = src.allocated_width();
    const int grain = std::max(32, height / 16);
    parallel_for_tracked(tbb::blocked_range<int>(0, height, grain),
                         [&](const tbb::blocked_range<int>& range) {
        for (int y = range.begin(); y < range.end(); ++y) {
            const Color* src_row = src_data + static_cast<std::size_t>(y) * alloc_w;
            uint8_t* dst_row = rgba8 + static_cast<std::size_t>(y) * width * 4;
            for (int x = 0; x < width; ++x) {
                const Color& c = src_row[x];
                if (apply_gamma) {
                    dst_row[x * 4 + 0] = Color::linear_to_srgb8(c.r);
                    dst_row[x * 4 + 1] = Color::linear_to_srgb8(c.g);
                    dst_row[x * 4 + 2] = Color::linear_to_srgb8(c.b);
                } else {
                    dst_row[x * 4 + 0] = static_cast<uint8_t>(std::clamp(c.r, 0.0f, 1.0f) * 255.0f + 0.5f);
                    dst_row[x * 4 + 1] = static_cast<uint8_t>(std::clamp(c.g, 0.0f, 1.0f) * 255.0f + 0.5f);
                    dst_row[x * 4 + 2] = static_cast<uint8_t>(std::clamp(c.b, 0.0f, 1.0f) * 255.0f + 0.5f);
                }
                dst_row[x * 4 + 3] = static_cast<uint8_t>(std::clamp(c.a, 0.0f, 1.0f) * 255.0f + 0.5f);
            }
        }
    });
}

}  // namespace chronon3d::video::packed
