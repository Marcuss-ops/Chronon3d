// SPDX-License-Identifier: MIT
#pragma once

#include <chronon3d/render_graph/compiler/pixel_domain.hpp>
#include <chronon3d/render_graph/core/node_identity.hpp>
#include <cstdint>
#include <span>
#include <vector>

namespace chronon3d::graph {

enum class PrimitiveType : std::uint8_t {
    Rect = 0,
    Glyph,
    Image,
    Mesh,
    Effect,
    Barrier,
    Custom,
};

/// Compact primitive header. Keeps primitive footprint minimal and cache-friendly.
struct GpuPrimitiveHeader {
    PrimitiveType type{PrimitiveType::Rect};
    std::uint8_t  flags{0};
    std::uint16_t clip_index{0};
    std::uint32_t transform_index{0};
    std::uint32_t resource_index{0};
    std::uint32_t parameter_index{0};
};

/// Rect / Quad primitive data payload
struct GpuRectPrimitive {
    float x{0.0f}, y{0.0f}, width{0.0f}, height{0.0f};
    std::uint32_t color_rgba{0xFFFFFFFF};
    float corner_radius{0.0f};
};

/// Glyph primitive data payload
struct GpuGlyphPrimitive {
    std::int32_t dst_x{0};
    std::int32_t dst_y{0};
    std::int32_t width{0};
    std::int32_t height{0};
    float scale_x{1.0f};
    float scale_y{1.0f};
    float opacity{1.0f};
    std::uint32_t rgba{0xFFFFFFFF};
    std::uint32_t atlas_page{0};
    float u0{0.0f}, v0{0.0f}, u1{1.0f}, v1{1.0f};
    float highlight_start{-1.0f};
    float highlight_end{-1.0f};
};

/// Image primitive data payload
struct GpuImagePrimitive {
    float src_x{0.0f}, src_y{0.0f}, src_w{0.0f}, src_h{0.0f};
    float dst_x{0.0f}, dst_y{0.0f}, dst_w{0.0f}, dst_h{0.0f};
    float opacity{1.0f};
    std::uint32_t sample_mode{0};
};

/// Batch of primitives executed in sequence with compatible GPU pipeline state.
struct PrimitiveBatch {
    std::uint32_t batch_id{0};
    std::uint32_t primitive_offset{0};
    std::uint32_t primitive_count{0};
    PixelDomain   domain{PixelDomain::NV12};
    std::uint32_t target_slot{0};
    bool          requires_barrier_after{false};
};

/// Unified primitive stream produced by the compiler.
struct PrimitiveStream {
    std::vector<GpuPrimitiveHeader> headers;
    std::vector<GpuRectPrimitive>   rects;
    std::vector<GpuGlyphPrimitive>  glyphs;
    std::vector<GpuImagePrimitive>  images;
    std::vector<PrimitiveBatch>     batches;

    [[nodiscard]] std::size_t total_primitives() const noexcept {
        return headers.size();
    }
    [[nodiscard]] std::size_t total_batches() const noexcept {
        return batches.size();
    }
    [[nodiscard]] bool empty() const noexcept {
        return headers.empty();
    }
};

} // namespace chronon3d::graph
