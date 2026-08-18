// SPDX-License-Identifier: MIT
//
// gpu_text_run.cpp — packed-atlas builder for the GPU text-run fast path.
// See gpu_text_run.hpp for the contract.

#include "gpu_text_run.hpp"

#include "../native_surface.hpp"

#include <chronon3d/runtime/render_surface.hpp>
#include <chronon3d/runtime/gpu_asset_cache.hpp>
#include <chronon3d/backends/text/text_render_resources.hpp>
#include <chronon3d/text/glyph_atlas.hpp>
#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <blend2d.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <string_view>
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

graph::RenderOpResult draw_packed_text_run_surface(
    RenderGraphContext& ctx,
    runtime::RenderSurfaceHandle destination,
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

    // Keep the packed atlas in the runtime-owned asset cache when available.
    // The bitmap bytes are the canonical identity: equal glyph pixels share
    // one device-local surface across frames and warm daemon jobs.
    const runtime::SurfaceDesc atlas_desc{
        dims.width, dims.height, runtime::PixelFormat::Rgba32Float,
        runtime::ResourceUsage::Storage,
        ctx.services.gpu_asset_cache
            ? runtime::LifetimeClass::JobPersistent
            : runtime::LifetimeClass::FrameTransient,
        0};
    runtime::RenderSurfaceHandle atlas_handle = runtime::kInvalidRenderSurfaceHandle;
    bool cached_atlas = false;
    if (ctx.services.gpu_asset_cache) {
        const std::string_view bytes(
            reinterpret_cast<const char*>(atlas_buffer.data()),
            atlas_buffer.size() * sizeof(float));
        runtime::GpuAssetKey key{
            assets::sha256_string(bytes),
            runtime::PixelFormat::Rgba32Float,
            dims.width,
            dims.height};
        const auto acquired = ctx.services.gpu_asset_cache->acquire(
            key, atlas_desc, atlas_buffer);
        atlas_handle = acquired.handle;
        cached_atlas = acquired.ok();
        if (!cached_atlas) {
            return graph::RenderOpResult(graph::RenderBackendError{
                graph::RenderBackendErrorCode::ExecutionFailure,
                "draw_packed_text_run: GPU atlas cache: " + acquired.error});
        }
    } else {
        atlas_handle = ctx.services.surface_registry->create(atlas_desc);
    }
    if (atlas_handle == runtime::kInvalidRenderSurfaceHandle) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::ExecutionFailure,
            "draw_packed_text_run: failed to create atlas surface"});
    }
    if (!cached_atlas) {
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
    }

    auto drawn = ctx.services.backend->draw_text_run_surface(
        destination, atlas_handle, instances);

    // Cached atlases belong to the runtime. Only the compatibility atlas is
    // released here.
    if (!cached_atlas) {
        (void)ctx.services.backend->release_surface(atlas_handle);
        ctx.services.surface_registry->release(atlas_handle);
    }

    if (!drawn.ok()) {
        return graph::RenderOpResult(graph::RenderBackendError{
            drawn.error().code, drawn.error().message});
    }
    return graph::RenderOpResult(graph::RenderOpOutcome{glyphs.size()});
}

graph::RenderOpResult draw_packed_text_run(
    RenderGraphContext& ctx,
    Framebuffer& destination,
    std::span<const GpuTextGlyph> glyphs) {
    if (!ensure_native_surface(ctx, destination)) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::ExecutionFailure,
            "draw_packed_text_run: destination has no native surface support"});
    }
    auto result = draw_packed_text_run_surface(
        ctx, destination.surface_handle(), glyphs);
    if (!result.ok()) release_native_surface(ctx, destination);
    return result;
}

graph::RenderOpResult draw_cached_text_run(
    RenderGraphContext& ctx,
    Framebuffer& destination,
    const TextRunShape& shape,
    const glm::mat4& model_matrix,
    float opacity) {
    if (!ctx.services.text_render_resources || !shape.layout ||
        shape.layout->placed.glyphs.empty() ||
        shape.glyphs.size() != shape.layout->placed.glyphs.size()) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::UnsupportedCapability,
            "draw_cached_text_run: CPU glyph atlas is not ready"});
    }

    // The surface text kernel consumes integer pixel quads. Keep the first
    // bridge deliberately affine/translation-only; animated opacity and
    // placement still update the instance buffer every frame.
    if (std::abs(model_matrix[0][1]) > 1e-4f ||
        std::abs(model_matrix[1][0]) > 1e-4f ||
        std::abs(model_matrix[0][0] - 1.0f) > 1e-4f ||
        std::abs(model_matrix[1][1] - 1.0f) > 1e-4f ||
        std::abs(model_matrix[3][3] - 1.0f) > 1e-4f) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::UnsupportedCapability,
            "draw_cached_text_run: non-translation transform"});
    }

    const auto& layout = *shape.layout;
    const int font_size = std::max(1, static_cast<int>(std::lround(layout.font_size)));
    std::vector<GpuTextGlyph> glyphs;
    glyphs.reserve(layout.placed.glyphs.size());
    for (std::size_t i = 0; i < layout.placed.glyphs.size(); ++i) {
        const auto& state = shape.glyphs[i];
        if (std::abs(state.scale.x - 1.0f) > 1e-4f ||
            std::abs(state.scale.y - 1.0f) > 1e-4f ||
            std::abs(state.rotation.x) > 1e-4f ||
            std::abs(state.rotation.y) > 1e-4f ||
            std::abs(state.rotation.z) > 1e-4f ||
            std::abs(state.skew) > 1e-4f || state.stroke.a > 1e-4f ||
            state.background.a > 1e-4f) {
            return graph::RenderOpResult(graph::RenderBackendError{
                graph::RenderBackendErrorCode::UnsupportedCapability,
                "draw_cached_text_run: animated/style feature requires legacy text path"});
        }
        const auto entry = ctx.services.text_render_resources->lookup_glyph_atlas(
            layout.font.font_path, state.glyph_id,
            static_cast<u32>(font_size));
        if (!entry || !entry->image || entry->image->empty()) {
            return graph::RenderOpResult(graph::RenderBackendError{
                graph::RenderBackendErrorCode::UnsupportedCapability,
                "draw_cached_text_run: glyph bitmap not prewarmed"});
        }
        BLImageData data{};
        if (entry->image->getData(&data) != BL_SUCCESS ||
            data.format != BL_FORMAT_PRGB32 || data.size.w <= 0 || data.size.h <= 0) {
            return graph::RenderOpResult(graph::RenderBackendError{
                graph::RenderBackendErrorCode::UnsupportedCapability,
                "draw_cached_text_run: unsupported glyph bitmap format"});
        }
        std::vector<float> rgba(static_cast<std::size_t>(data.size.w) * data.size.h * 4);
        for (int y = 0; y < data.size.h; ++y) {
            const auto* row = static_cast<const std::uint8_t*>(data.pixelData) +
                              static_cast<std::size_t>(y) * data.stride;
            for (int x = 0; x < data.size.w; ++x) {
                const auto* px = row + static_cast<std::size_t>(x) * 4;
                const std::size_t off = (static_cast<std::size_t>(y) * data.size.w + x) * 4;
                rgba[off + 0] = px[2] / 255.0f;
                rgba[off + 1] = px[1] / 255.0f;
                rgba[off + 2] = px[0] / 255.0f;
                rgba[off + 3] = px[3] / 255.0f;
            }
        }
        const auto& placed = layout.placed.glyphs[i];
        glyphs.push_back(GpuTextGlyph{
            static_cast<std::uint32_t>(data.size.w),
            static_cast<std::uint32_t>(data.size.h),
            std::move(rgba),
            static_cast<std::int32_t>(std::lround(model_matrix[3][0] +
                placed.x + state.position.x + entry->x_offset)),
            static_cast<std::int32_t>(std::lround(model_matrix[3][1] +
                placed.y + state.position.y + entry->y_offset)),
            state.opacity * opacity});
    }
    if (!ensure_native_surface(ctx, destination)) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::ExecutionFailure,
            "draw_cached_text_run: destination has no native surface"});
    }

    // caption_card uses one soft shadow. Render the alpha mask into a
    // transient device-local surface and let the canonical Vulkan glow pass
    // blur/composite it; no framebuffer download is needed. More complex
    // shadow stacks remain on the reference path.
    if (!shape.shadows.empty()) {
        if (shape.shadows.size() != 1 || shape.dissolve_layout) {
            release_native_surface(ctx, destination);
            return graph::RenderOpResult(graph::RenderBackendError{
                graph::RenderBackendErrorCode::UnsupportedCapability,
                "draw_cached_text_run: unsupported native shadow stack"});
        }
        const auto& shadow = shape.shadows.front();
        std::vector<GpuTextGlyph> shadow_glyphs = glyphs;
        for (auto& glyph : shadow_glyphs) {
            for (std::size_t i = 0; i + 3 < glyph.rgba.size(); i += 4) {
                const float alpha = glyph.rgba[i + 3];
                glyph.rgba[i + 0] = 0.0f;
                glyph.rgba[i + 1] = 0.0f;
                glyph.rgba[i + 2] = 0.0f;
                glyph.rgba[i + 3] = alpha;
            }
            glyph.dst_x += static_cast<std::int32_t>(std::lround(shadow.offset.x));
            glyph.dst_y += static_cast<std::int32_t>(std::lround(shadow.offset.y));
            glyph.opacity *= shadow.opacity;
        }

        const runtime::SurfaceDesc desc = native_surface_desc(
            destination.width(), destination.height());
        std::vector<float> clear(static_cast<std::size_t>(destination.width()) *
                                 destination.height() * 4, 0.0f);
        std::array<runtime::RenderSurfaceHandle, 3> scratch{};
        bool ready = true;
        for (auto& handle : scratch) {
            handle = ctx.services.surface_registry->create(desc);
            if (handle == runtime::kInvalidRenderSurfaceHandle ||
                !ctx.services.backend->create_surface(handle, desc).ok() ||
                !ctx.services.backend->upload_surface(handle, desc, clear).ok()) {
                ready = false;
                break;
            }
        }
        auto cleanup = [&]() {
            for (auto handle : scratch) {
                if (handle != runtime::kInvalidRenderSurfaceHandle) {
                    (void)ctx.services.backend->release_surface(handle);
                    ctx.services.surface_registry->release(handle);
                }
            }
        };
        if (!ready || !draw_packed_text_run_surface(ctx, scratch[0], shadow_glyphs).ok() ||
            !ctx.services.backend->glow_surfaces(
                destination.surface_handle(), scratch[0], scratch[1], scratch[2],
                shadow.blur, shadow.opacity, shadow.color).ok()) {
            cleanup();
            release_native_surface(ctx, destination);
            return graph::RenderOpResult(graph::RenderBackendError{
                graph::RenderBackendErrorCode::UnsupportedCapability,
                "draw_cached_text_run: native shadow execution failed"});
        }
        cleanup();
    }

    return draw_packed_text_run_surface(
        ctx, destination.surface_handle(), glyphs);
}

} // namespace chronon3d::graph::text_run
