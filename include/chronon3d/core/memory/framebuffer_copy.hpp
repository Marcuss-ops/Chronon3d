#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>

#include <algorithm>
#include <cstring>

namespace chronon3d {

// =============================================================================
// FramebufferCopyAuthority — single implementation of full-frame CPU pixel
// copies between framebuffer storages.
//
// Framebuffer rows are NOT contiguous at `width * sizeof(Color)`:
//   * owned storage is cache-line padded (stride = align_stride_to_cache_line),
//   * pool storage is additionally size-class rounded (round_to_bucket),
//   * external/arena storage can carry any caller-chosen stride.
// A flat [data, data + width*height) copy is valid ONLY when both buffers are
// entirely stride-free; any padding shifts every row after the first, which
// renders striped/corrupted output.
//
// Every full-frame framebuffer→framebuffer copy in the render graph routes
// through this authority so shared and owned acquisition paths share exactly
// one implementation:
//   * RenderGraphContext::acquire_framebuffer(const Framebuffer&)
//   * RenderGraphContext::acquire_owned_fb(const Framebuffer&)
//   * FramebufferPool::acquire_from(const Framebuffer&)
//   * scroll/copy-previous-surface reuse copies
//
// Zero-copy storage transfer (Wave 6) is NOT part of this authority: the
// swap-transfer decision lives in `can_zero_copy_transfer(policy, ...)`.
// Copy callers keep their own metadata propagation (origin/opaque/
// content-cleared/surface/cpu-authoritative) after copying pixels.
// =============================================================================

class FramebufferCopyAuthority {
public:
    // Row-wise copy of a copy_width×copy_height rectangle between two
    // buffers that may use different row strides (strides are measured in
    // Color elements). No-op when the rectangle is empty, either pointer is
    // null, or the buffers are the same allocation.
    //
    // Each row is copied independently, so mixed strides never shift rows.
    // Precondition: the source and destination allocations do not overlap
    // (identical allocations are explicitly allowed and skipped).
    static void copy_rows(
        Color* dst, i32 dst_stride,
        const Color* src, i32 src_stride,
        i32 copy_width, i32 copy_height) noexcept
    {
        if (copy_width <= 0 || copy_height <= 0) return;
        if (!dst || !src || dst == src) return;
        if (dst_stride <= 0 || src_stride <= 0) return;

        const usize row_bytes = static_cast<usize>(copy_width) * sizeof(Color);
        Color* d = dst;
        const Color* s = src;
        for (i32 y = 0; y < copy_height; ++y) {
            std::memcpy(d, s, row_bytes);
            d += static_cast<usize>(dst_stride);
            s += static_cast<usize>(src_stride);
        }
    }

    // Full-frame copy between two Framebuffers with possibly different
    // strides. Copies the overlapping logical area (min of each dimension)
    // using each buffer's own row stride. Writing through the destination's
    // mutable row accessor invalidates its `content_cleared` flag exactly as
    // the previous inline copies did; callers that need to restore metadata
    // do so after the copy (see FramebufferPool::acquire_from).
    static void copy_rows(Framebuffer& dst, const Framebuffer& src) noexcept {
        const i32 copy_width = std::min(src.width(), dst.width());
        const i32 copy_height = std::min(src.height(), dst.height());
        if (copy_width <= 0 || copy_height <= 0) return;
        if (dst.data() == src.data()) return;
        copy_rows(dst.pixels_row(0), dst.stride(),
                  src.pixels_row(0), src.stride(),
                  copy_width, copy_height);
    }
};

} // namespace chronon3d
