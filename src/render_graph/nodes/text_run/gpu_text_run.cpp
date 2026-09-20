// SPDX-License-Identifier: MIT
//
// gpu_text_run.cpp — packed-atlas surface upload for the GPU text-run fast
// path. See gpu_text_run.hpp for the contract.
//
// Split (P2 structure pass): the animator lowering/sampling now lives in
// gpu_text_run_animation.cpp, the shared packing/highlight helpers in
// gpu_text_run_packed.cpp, the MTSDF preparation/dispatch in
// gpu_text_run_mtsdf.cpp and the compatibility bridge in
// gpu_text_run_cached.cpp. This TU owns only the packed-atlas surface path.

#include "gpu_text_run.hpp"
#include "gpu_text_run_detail.hpp"

#include "../native_surface.hpp"

#include <chronon3d/runtime/render_surface.hpp>
#include <chronon3d/runtime/gpu_asset_cache.hpp>
#include <chronon3d/runtime/gpu_text_atlas_cache.hpp>
#include <chronon3d/core/profiling/profiling.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace chronon3d::graph::text_run {

graph::RenderOpResult draw_packed_text_run_surface(
    RenderGraphContext& ctx,
    runtime::RenderSurfaceHandle destination,
    std::span<const GpuTextGlyph> glyphs,
    float current_frame,
    const Color& highlight_color,
    bool highlight_enabled) {
    if (glyphs.empty()) {
        // Bug fix on main: returning RenderOpOutcome{0} here caused the
        // upstream caller in text_run_execution.cpp:101-102 to treat this
        // path as success-with-zero-items and skip the legacy backend.draw_text_run()
        // fallback, which produced all-black frames for every preset-driven render.
        // Returning an UnsupportedCapability error forces the caller to fall
        // back to the legacy text path that actually draws pixels.
        return graph::RenderOpResult(graph::make_render_backend_error(
            graph::RenderBackendErrorCode::UnsupportedCapability,
            "draw_packed_text_run_surface: empty glyph vector; fall back to legacy text path"));
    }
    if (!ctx.backend || !ctx.surface_registry) {
        return graph::RenderOpResult(graph::make_render_backend_error(
            graph::RenderBackendErrorCode::UnsupportedCapability,
            "draw_packed_text_run: no backend or surface registry"));
    }

    // Reject malformed glyph buffers before touching the device so the
    // caller can fall back to draw_text_run without leaking a surface.
    for (const auto& glyph : glyphs) {
        const auto pixels = glyph.pixels();
        std::size_t quad = 0;
        std::size_t expected = 0;
        if (glyph.width == 0 || glyph.height == 0 ||
            !detail::checked_mul(static_cast<std::size_t>(glyph.width),
                         static_cast<std::size_t>(glyph.height), quad) ||
            !detail::checked_mul(quad, 4u, expected) ||
            pixels.size() != expected) {
            return graph::RenderOpResult(graph::make_render_backend_error(
                graph::RenderBackendErrorCode::InvalidInput,
                "draw_packed_text_run: glyph rgba size mismatch"));
        }
    }

    std::vector<float> atlas_buffer;
    std::vector<runtime::GlyphInstance> instances;
    instances.reserve(glyphs.size());
    runtime::SurfaceDesc atlas_desc{};
    runtime::RenderSurfaceHandle atlas_handle = runtime::kInvalidRenderSurfaceHandle;
    bool cached_atlas = false;
    runtime::PackedTextAtlas persistent_atlas{};

    // The bitmap payload is immutable for a prepared/styled run.  Keep the
    // packed GPU surface persistent and rebuild only GlyphInstance records,
    // whose position/opacity/highlight fields are frame-dynamic.
    if (ctx.gpu_text_atlas_cache) {
        std::vector<runtime::PackedGlyphBitmap> bitmap_views;
        bitmap_views.reserve(glyphs.size());
        std::uint64_t key_bytes_hashed = 0;
        for (const auto& glyph : glyphs) {
            const auto pixels = glyph.pixels();
            bitmap_views.push_back(runtime::PackedGlyphBitmap{
                glyph.width, glyph.height, pixels});
            key_bytes_hashed += sizeof(glyph.width) + sizeof(glyph.height) +
                static_cast<std::uint64_t>(pixels.size_bytes());
        }
        const auto acquire_start = profiling::now();
        const bool acquired = ctx.gpu_text_atlas_cache->acquire(
            bitmap_views, persistent_atlas);
        if (profiling::g_current_counters) {
            auto* counters = profiling::g_current_counters;
            counters->gpu_text_atlas_cache_acquire_wall_us.fetch_add(
                static_cast<std::uint64_t>(profiling::elapsed_us(acquire_start)),
                std::memory_order_relaxed);
            counters->gpu_text_atlas_key_bytes_hashed.fetch_add(
                key_bytes_hashed, std::memory_order_relaxed);
            if (acquired && persistent_atlas.cache_hit) {
                counters->gpu_text_atlas_cache_hits.fetch_add(
                    1, std::memory_order_relaxed);
            } else {
                counters->gpu_text_atlas_cache_misses.fetch_add(
                    1, std::memory_order_relaxed);
            }
        }
        if (acquired && persistent_atlas.valid()) {
            cached_atlas = true;
            atlas_handle = persistent_atlas.handle;
            atlas_desc = runtime::SurfaceDesc::make(
                persistent_atlas.width, persistent_atlas.height,
                runtime::PixelFormat::Rgba32Float,
                runtime::ResourceUsage::Storage,
                runtime::LifetimeClass::JobPersistent);
            for (std::size_t i = 0; i < glyphs.size(); ++i) {
                const auto& glyph = glyphs[i];
                instances.push_back(runtime::GlyphInstance{
                    glyph.dst_x, glyph.dst_y,
                    static_cast<std::int32_t>(persistent_atlas.origin_x[i]),
                    static_cast<std::int32_t>(persistent_atlas.origin_y[i]),
                    static_cast<std::int32_t>(glyph.width),
                    static_cast<std::int32_t>(glyph.height),
                    glyph.opacity, glyph.scale_x, glyph.scale_y, 0.0f,
                    glyph.highlight_start_frame,
                    glyph.highlight_end_frame});
            }
        }
    }

    if (!cached_atlas) {
        const auto pack_start = profiling::now();
        const auto [packed, dims] = detail::pack_glyphs(glyphs);
        if (profiling::g_current_counters) {
            profiling::g_current_counters->gpu_text_atlas_pack_wall_us.fetch_add(
                static_cast<std::uint64_t>(profiling::elapsed_us(pack_start)),
                std::memory_order_relaxed);
        }

        // Build the packed atlas buffer and per-glyph instances in one pass.
        std::size_t atlas_pixels = 0;
        std::size_t atlas_bytes = 0;
        if (!detail::checked_mul(static_cast<std::size_t>(dims.width),
                         static_cast<std::size_t>(dims.height), atlas_pixels) ||
            !detail::checked_mul(atlas_pixels, 4u, atlas_bytes)) {
            return graph::RenderOpResult(graph::make_render_backend_error(
                graph::RenderBackendErrorCode::InvalidInput,
                "draw_packed_text_run_surface: atlas dimensions overflow"));
        }
        const auto pixel_copy_start = profiling::now();
        atlas_buffer.assign(atlas_bytes, 0.0f);
        atlas_desc = runtime::SurfaceDesc::make(
            dims.width, dims.height, runtime::PixelFormat::Rgba32Float,
            runtime::ResourceUsage::Storage,
            ctx.gpu_text_atlas_cache != nullptr ? runtime::LifetimeClass::JobPersistent
                                   : runtime::LifetimeClass::FrameTransient);
        for (std::size_t i = 0; i < glyphs.size(); ++i) {
            const auto& glyph = glyphs[i];
            const auto& place = packed[i];
            for (std::uint32_t row = 0; row < glyph.height; ++row) {
                const std::size_t src_off =
                    static_cast<std::size_t>(row) * glyph.width * 4;
                const std::size_t dst_off =
                    (static_cast<std::size_t>(place.atlas_y + row) * dims.width +
                     place.atlas_x) * 4;
                const auto pixels = glyph.pixels();
                std::copy_n(pixels.data() + src_off,
                            static_cast<std::size_t>(glyph.width) * 4,
                            atlas_buffer.data() + dst_off);
            }
            instances.push_back(runtime::GlyphInstance{
                glyph.dst_x, glyph.dst_y,
                static_cast<std::int32_t>(place.atlas_x),
                static_cast<std::int32_t>(place.atlas_y),
                static_cast<std::int32_t>(glyph.width),
                static_cast<std::int32_t>(glyph.height),
                glyph.opacity, glyph.scale_x, glyph.scale_y, 0.0f,
                glyph.highlight_start_frame,
                glyph.highlight_end_frame});
        }
        if (profiling::g_current_counters) {
            profiling::g_current_counters->gpu_text_atlas_pixel_copy_wall_us.fetch_add(
                static_cast<std::uint64_t>(profiling::elapsed_us(pixel_copy_start)),
                std::memory_order_relaxed);
        }
    }

    // Keep fallback costs separate from persistent-atlas hits. A cache miss
    // still performs one preparation-time pack/upload; steady-state hits do
    // neither and only submit dynamic instances.
    if (profiling::g_current_counters) {
        if (!cached_atlas || !persistent_atlas.cache_hit) {
            const auto repack_bytes = atlas_buffer.empty()
                ? static_cast<std::uint64_t>(atlas_desc.bytes)
                : static_cast<std::uint64_t>(atlas_buffer.size() * sizeof(float));
            profiling::g_current_counters->gpu_text_atlas_repack_count.fetch_add(
                1, std::memory_order_relaxed);
            profiling::g_current_counters->gpu_text_atlas_repack_bytes.fetch_add(
                repack_bytes, std::memory_order_relaxed);
        }
    }

    if (!cached_atlas) {
        // Compatibility path when no runtime cache is available or the
        // persistent upload was rejected. The runtime cache owns all handles
        // in the normal production path.
        atlas_handle = ctx.surface_registry->create(atlas_desc);
        if (atlas_handle == runtime::kInvalidRenderSurfaceHandle) {
            return graph::RenderOpResult(graph::make_render_backend_error(
                graph::RenderBackendErrorCode::ExecutionFailure,
                "draw_packed_text_run: failed to create atlas surface"));
        }
        auto created = ctx.backend->create_surface(atlas_handle, atlas_desc);
        if (!created.ok()) {
            ctx.surface_registry->release(atlas_handle);
            return graph::RenderOpResult(graph::make_render_backend_error(
                graph::RenderBackendErrorCode::ExecutionFailure,
                "draw_packed_text_run: create_surface: " + created.error().message));
        }
        profiling::GpuUploadProducerScope upload_scope(
            profiling::GpuUploadProducer::Text);
        auto uploaded = ctx.backend->upload_surface(
            atlas_handle, atlas_desc, atlas_buffer);
        if (!uploaded.ok()) {
            (void)ctx.backend->release_surface(atlas_handle);
            ctx.surface_registry->release(atlas_handle);
            return graph::RenderOpResult(graph::make_render_backend_error(
                graph::RenderBackendErrorCode::ExecutionFailure,
                "draw_packed_text_run: upload_surface: " + uploaded.error().message));
        }
        if (profiling::g_current_counters) {
            profiling::g_current_counters->gpu_text_atlas_upload_count.fetch_add(
                1, std::memory_order_relaxed);
            profiling::g_current_counters->gpu_text_atlas_upload_bytes.fetch_add(
                static_cast<std::uint64_t>(atlas_buffer.size() * sizeof(float)),
                std::memory_order_relaxed);
        }
    } else if (persistent_atlas.uploaded && profiling::g_current_counters) {
        profiling::g_current_counters->gpu_text_atlas_upload_count.fetch_add(
            1, std::memory_order_relaxed);
        profiling::g_current_counters->gpu_text_atlas_upload_bytes.fetch_add(
            static_cast<std::uint64_t>(atlas_desc.bytes),
            std::memory_order_relaxed);
    }

    auto drawn = ctx.backend->draw_text_run_surface_timed(
        destination, atlas_handle, instances, current_frame,
        highlight_color, highlight_enabled);


    if (profiling::g_current_counters) {
        profiling::g_current_counters->gpu_text_instance_upload_count.fetch_add(
            1, std::memory_order_relaxed);
        profiling::g_current_counters->gpu_text_instance_upload_bytes.fetch_add(
            static_cast<std::uint64_t>(instances.size() * sizeof(runtime::GlyphInstance)),
            std::memory_order_relaxed);
    }

    // Cached atlases belong to the runtime. Only the compatibility atlas is
    // released here.
    if (!cached_atlas) {
        (void)ctx.backend->release_surface(atlas_handle);
        ctx.surface_registry->release(atlas_handle);
    }

    if (!drawn.ok()) {
        return graph::RenderOpResult(graph::make_render_backend_error(
            drawn.error().code, drawn.error().message));
    }
    return graph::RenderOpResult(graph::RenderOpOutcome{glyphs.size()});
}

graph::RenderOpResult draw_packed_text_run(
    RenderGraphContext& ctx,
    Framebuffer& destination,
    std::span<const GpuTextGlyph> glyphs,
    float current_frame,
    const Color& highlight_color,
    bool highlight_enabled) {
    if (glyphs.empty()) {
        return graph::RenderOpResult(graph::RenderOpOutcome{0});
    }
    // Validate glyph payloads before allocating or clearing a native surface.
    // This keeps malformed input a deterministic InvalidInput result even when
    // the destination has no backend surface yet.
    for (const auto& glyph : glyphs) {
        const auto pixels = glyph.pixels();
        std::size_t quad = 0;
        std::size_t expected = 0;
        if (glyph.width == 0 || glyph.height == 0 ||
            !detail::checked_mul(static_cast<std::size_t>(glyph.width),
                         static_cast<std::size_t>(glyph.height), quad) ||
            !detail::checked_mul(quad, 4u, expected) ||
            pixels.size() != expected) {
            return graph::RenderOpResult(graph::make_render_backend_error(
                graph::RenderBackendErrorCode::InvalidInput,
                "draw_packed_text_run: glyph rgba size mismatch"));
        }
    }
    // TextRun output is an independent transparent surface. Avoid uploading
    // the CPU framebuffer before the glyph pass; this was a full-canvas
    // CPU->GPU transfer for every watermark/subtitle layer and frame.
    if (!ensure_empty_native_surface(ctx, destination)) {
        return graph::RenderOpResult(graph::make_render_backend_error(
            graph::RenderBackendErrorCode::UnsupportedCapability,
            "draw_packed_text_run: destination has no native surface support"));
    }
    const auto cleared = ctx.backend->fill_rect_surface(
        destination.surface_handle(), 0, 0, destination.width(), destination.height(),
        Color::transparent());
    if (!cleared.ok()) return cleared;
    auto result = draw_packed_text_run_surface(
        ctx, destination.surface_handle(), glyphs, current_frame,
        highlight_color, highlight_enabled);
    if (!result.ok()) release_native_surface(ctx, destination);
    return result;
}

} // namespace chronon3d::graph::text_run
