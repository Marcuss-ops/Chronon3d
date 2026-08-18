// SPDX-License-Identifier: MIT
//
// gpu_text_run.cpp — packed-atlas builder for the GPU text-run fast path.
// See gpu_text_run.hpp for the contract.

#include "gpu_text_run.hpp"

#include "../native_surface.hpp"

#include <chronon3d/runtime/render_surface.hpp>

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace chronon3d::graph::text_run {

namespace {

/// Fixed packed-atlas width.  Glyphs are shelf-packed left-to-right and
/// wrapped to a new row when a glyph would overflow this width.
constexpr std::uint32_t kMaxAtlasWidth = 1024;

struct PackedGlyph {
    std::uint32_t atlas_x{0};
    std::uint32_t atlas_y{0};
};

struct AtlasDims {
    std::uint32_t width{0};
    std::uint32_t height{0};
};

/// Shelf-pack the glyph quads.  Returns the per-glyph atlas origins (in the
/// same order as `glyphs`) plus the resulting atlas dimensions (at least 1x1).
std::pair<std::vector<PackedGlyph>, AtlasDims> pack_glyphs(
    std::span<const GpuTextGlyph> glyphs) {
    std::vector<PackedGlyph> packed;
    packed.reserve(glyphs.size());

    std::uint32_t cur_x = 0;
    std::uint32_t cur_y = 0;
    std::uint32_t row_height = 0;
    std::uint32_t atlas_width = 1;

    for (const auto& glyph : glyphs) {
        const std::uint32_t w = std::max(1u, glyph.width);
        const std::uint32_t h = std::max(1u, glyph.height);
        if (cur_x + w > kMaxAtlasWidth && cur_x > 0) {
            cur_x = 0;
            cur_y += row_height;
            row_height = 0;
        }
        packed.push_back(PackedGlyph{cur_x, cur_y});
        cur_x += w;
        row_height = std::max(row_height, h);
        atlas_width = std::max(atlas_width, cur_x);
    }
    const std::uint32_t atlas_height = std::max(1u, cur_y + row_height);
    return {std::move(packed), AtlasDims{atlas_width, atlas_height}};
}

} // namespace

graph::RenderOpResult draw_packed_text_run(
    RenderGraphContext& ctx,
    Framebuffer& destination,
    std::span<const GpuTextGlyph> glyphs) {
    if (glyphs.empty()) {
        return graph::RenderOpResult(graph::RenderOpOutcome{0});
    }
    if (!ctx.services.backend || !ctx.services.surface_registry) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::UnsupportedCapability,
            "draw_packed_text_run: no backend or surface registry"});
    }

    // Reject malformed glyph buffers before touching the device so the
    // caller can fall back to draw_text_run without leaking a surface.
    for (const auto& glyph : glyphs) {
        const std::size_t expected =
            static_cast<std::size_t>(glyph.width) * glyph.height * 4;
        if (glyph.width == 0 || glyph.height == 0 ||
            glyph.rgba.size() != expected) {
            return graph::RenderOpResult(graph::RenderBackendError{
                graph::RenderBackendErrorCode::InvalidInput,
                "draw_packed_text_run: glyph rgba size mismatch"});
        }
    }

    const auto [packed, dims] = pack_glyphs(glyphs);

    // Build the packed atlas buffer and the per-glyph instances in one pass.
    std::vector<float> atlas_buffer(
        static_cast<std::size_t>(dims.width) * dims.height * 4, 0.0f);
    std::vector<runtime::GlyphInstance> instances;
    instances.reserve(glyphs.size());
    for (std::size_t i = 0; i < glyphs.size(); ++i) {
        const auto& glyph = glyphs[i];
        const auto& place = packed[i];
        for (std::uint32_t row = 0; row < glyph.height; ++row) {
            const std::size_t src_off =
                static_cast<std::size_t>(row) * glyph.width * 4;
            const std::size_t dst_off =
                (static_cast<std::size_t>(place.atlas_y + row) * dims.width +
                 place.atlas_x) * 4;
            std::copy_n(glyph.rgba.data() + src_off,
                        static_cast<std::size_t>(glyph.width) * 4,
                        atlas_buffer.data() + dst_off);
        }
        instances.push_back(runtime::GlyphInstance{
            glyph.dst_x,
            glyph.dst_y,
            static_cast<std::int32_t>(place.atlas_x),
            static_cast<std::int32_t>(place.atlas_y),
            static_cast<std::int32_t>(glyph.width),
            static_cast<std::int32_t>(glyph.height),
            glyph.opacity,
            0.0f});
    }

    // Create + upload the transient packed atlas surface.
    const runtime::SurfaceDesc atlas_desc{
        dims.width, dims.height, runtime::PixelFormat::Rgba32Float,
        runtime::ResourceUsage::Storage, runtime::LifetimeClass::FrameTransient,
        0};
    const auto atlas_handle = ctx.services.surface_registry->create(atlas_desc);
    if (atlas_handle == runtime::kInvalidRenderSurfaceHandle) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::ExecutionFailure,
            "draw_packed_text_run: failed to create atlas surface"});
    }
    auto created = ctx.services.backend->create_surface(atlas_handle, atlas_desc);
    if (!created.ok()) {
        ctx.services.surface_registry->release(atlas_handle);
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::ExecutionFailure,
            "draw_packed_text_run: create_surface: " + created.error().message});
    }
    auto uploaded = ctx.services.backend->upload_surface(
        atlas_handle, atlas_desc, atlas_buffer);
    if (!uploaded.ok()) {
        (void)ctx.services.backend->release_surface(atlas_handle);
        ctx.services.surface_registry->release(atlas_handle);
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::ExecutionFailure,
            "draw_packed_text_run: upload_surface: " + uploaded.error().message});
    }

    // Ensure the destination carries a native surface, then dispatch.
    if (!ensure_native_surface(ctx, destination)) {
        (void)ctx.services.backend->release_surface(atlas_handle);
        ctx.services.surface_registry->release(atlas_handle);
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::ExecutionFailure,
            "draw_packed_text_run: destination has no native surface support"});
    }

    auto drawn = ctx.services.backend->draw_text_run_surface(
        destination.surface_handle(), atlas_handle, instances);

    // The atlas is frame-transient; release it regardless of the draw result.
    (void)ctx.services.backend->release_surface(atlas_handle);
    ctx.services.surface_registry->release(atlas_handle);

    if (!drawn.ok()) {
        release_native_surface(ctx, destination);
        return graph::RenderOpResult(graph::RenderBackendError{
            drawn.error().code, drawn.error().message});
    }
    return graph::RenderOpResult(graph::RenderOpOutcome{glyphs.size()});
}

} // namespace chronon3d::graph::text_run
