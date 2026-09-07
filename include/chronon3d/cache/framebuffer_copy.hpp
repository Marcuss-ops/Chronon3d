#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>

#include <algorithm>
#include <cstring>

namespace chronon3d {

class FramebufferCopyAuthority {
public:
    static void copy_rows(
        Color* dst, i32 dst_stride, const Color* src, i32 src_stride,
        i32 copy_width, i32 copy_height) noexcept {
        if (copy_width <= 0 || copy_height <= 0 || !dst || !src || dst == src) return;
        if (dst_stride <= 0 || src_stride <= 0) return;
        const usize row_bytes = static_cast<usize>(copy_width) * sizeof(Color);
        for (i32 y = 0; y < copy_height; ++y) {
            std::memcpy(dst, src, row_bytes);
            dst += static_cast<usize>(dst_stride);
            src += static_cast<usize>(src_stride);
        }
    }

    static void copy_rows(Framebuffer& dst, const Framebuffer& src) noexcept {
        const i32 copy_width = std::min(src.width(), dst.width());
        const i32 copy_height = std::min(src.height(), dst.height());
        if (copy_width <= 0 || copy_height <= 0 || dst.data() == src.data()) return;
        copy_rows(dst.pixels_row(0), dst.stride(), src.pixels_row(0), src.stride(),
                  copy_width, copy_height);
    }
};

}  // namespace chronon3d
