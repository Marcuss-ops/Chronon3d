// SPDX-License-Identifier: MIT
//
// gpu_text_run.cpp — packed-atlas builder for the GPU text-run fast path.
// See gpu_text_run.hpp for the contract.

#include "gpu_text_run.hpp"

#include "../native_surface.hpp"

#include <chronon3d/runtime/render_surface.hpp>
#include <chronon3d/runtime/gpu_asset_cache.hpp>
#include <chronon3d/runtime/gpu_text_atlas_cache.hpp>
#include <chronon3d/backends/text/text_render_resources.hpp>
#include <chronon3d/text/glyph_atlas.hpp>
#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
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

std::optional<GlyphAtlasEntry> rasterize_missing_glyph(
    TextRenderResources& resources,
    const assets::AssetResolver& resolver,
    const std::string& font_path,
    std::uint32_t glyph_id,
    std::uint32_t font_size,
    const Color& color) {
    const auto handle = resources.resolve_handle(font_path,
        static_cast<float>(font_size), resolver);
    if (!handle.valid()) return std::nullopt;
    BLFont font;
    if (font.createFromFace(*handle.bl_face, static_cast<float>(font_size)) != BL_SUCCESS) {
        return std::nullopt;
    }
    BLBoxI bounds{};
    if (font.getGlyphBounds(&glyph_id, 0, &bounds, 1) != BL_SUCCESS) return std::nullopt;
    const double units_per_em = static_cast<double>(handle.bl_face->unitsPerEm());
    if (units_per_em <= 0.0) return std::nullopt;
    const double kGlyphUnit = static_cast<double>(font_size) / units_per_em;
    const int x0 = static_cast<int>(std::floor(bounds.x0 * kGlyphUnit));
    const int y0 = static_cast<int>(std::floor(bounds.y0 * kGlyphUnit));
    const int x1 = static_cast<int>(std::ceil(bounds.x1 * kGlyphUnit));
    const int y1 = static_cast<int>(std::ceil(bounds.y1 * kGlyphUnit));
    const int width = x1 - x0;
    const int height = y1 - y0;
    if (width <= 0 || height <= 0) return std::nullopt;

    auto image = std::make_shared<BLImage>(width, height, BL_FORMAT_PRGB32);
    BLContext context(*image);
    context.setCompOp(BL_COMP_OP_SRC_COPY);
    context.setFillStyle(BLRgba32(0, 0, 0, 0));
    context.fillAll();
    context.setCompOp(BL_COMP_OP_SRC_OVER);
    context.setFillStyle(BLRgba32(
        static_cast<std::uint32_t>(std::clamp(color.b, 0.0f, 1.0f) * 255.0f),
        static_cast<std::uint32_t>(std::clamp(color.g, 0.0f, 1.0f) * 255.0f),
        static_cast<std::uint32_t>(std::clamp(color.r, 0.0f, 1.0f) * 255.0f),
        static_cast<std::uint32_t>(std::clamp(color.a, 0.0f, 1.0f) * 255.0f)));
    BLGlyphPlacement placement{};
    placement.placement.reset(0.0, 0.0);
    placement.advance.reset(0.0, 0.0);
    BLGlyphRun run{};
    run.glyphData = &glyph_id;
    run.glyphAdvance = static_cast<int8_t>(sizeof(glyph_id));
    run.placementData = &placement;
    run.placementAdvance = static_cast<int8_t>(sizeof(placement));
    run.placementType = BL_GLYPH_PLACEMENT_TYPE_ADVANCE_OFFSET;
    run.size = 1;
    context.fillGlyphRun(BLPoint(-x0, -y0), font, run);
    context.end();

    GlyphAtlasEntry entry;
    entry.image = std::move(image);
    entry.x_offset = x0;
    entry.y_offset = y0;
    entry.fill_color_rgba =
        (static_cast<std::uint32_t>(std::clamp(color.r, 0.0f, 1.0f) * 255.0f) << 24u) |
        (static_cast<std::uint32_t>(std::clamp(color.g, 0.0f, 1.0f) * 255.0f) << 16u) |
        (static_cast<std::uint32_t>(std::clamp(color.b, 0.0f, 1.0f) * 255.0f) << 8u) |
        static_cast<std::uint32_t>(std::clamp(color.a, 0.0f, 1.0f) * 255.0f);
    return entry;
}

struct TimedHighlightPlan {
    Color color{Color::yellow()};
    std::vector<std::pair<float, float>> glyph_ranges;
    bool enabled{false};
};

TimedHighlightPlan build_timed_highlight_plan(const TextRunShape& shape) {
    TimedHighlightPlan plan;
    if (!shape.layout || shape.layout->units.word_count == 0) return plan;
    plan.glyph_ranges.assign(shape.layout->placed.glyphs.size(), {-1.0f, -1.0f});
    const auto word_count = static_cast<float>(shape.layout->units.word_count);

    for (const auto& animator : shape.animators) {
        std::optional<Color> animator_color;
        for (const auto& property : animator.properties) {
            if (const auto* fill = std::get_if<FillColorProperty>(&property)) {
                animator_color = fill->color;
                break;
            }
        }
        const bool karaoke_named = animator.id.find("karaoke") != std::string::npos;
        // The GPU timed path is intentionally limited to karaoke animators.
        // Other word animators (for example active_word_pop) may also carry a
        // fill property but additionally change scale/background/stroke and
        // must keep the legacy per-glyph evaluation semantics.
        if (!karaoke_named) continue;

        for (const auto& selector : animator.selectors) {
            if (selector.unit != TextSelectorUnit::Word) continue;
            const auto& keyframes = selector.amount.keyframes();
            float start_frame = -1.0f;
            float end_frame = -1.0f;
            for (const auto& keyframe : keyframes) {
                if (start_frame < 0.0f && keyframe.value >= 99.0f) {
                    start_frame = static_cast<float>(keyframe.frame.integral());
                } else if (start_frame >= 0.0f && keyframe.value <= 1.0f) {
                    end_frame = static_cast<float>(keyframe.frame.integral());
                    break;
                }
            }
            if (start_frame < 0.0f || end_frame < start_frame) continue;

            const float first_word = std::clamp(
                selector.start.evaluate(0.0) * word_count / 100.0f,
                0.0f, word_count);
            const float last_word = std::clamp(
                selector.end.evaluate(0.0) * word_count / 100.0f,
                0.0f, word_count);
            for (std::size_t glyph = 0; glyph < plan.glyph_ranges.size(); ++glyph) {
                const auto word = static_cast<float>(
                    shape.layout->units.glyph_to_word[glyph]);
                if (word >= first_word && word < last_word &&
                    plan.glyph_ranges[glyph].first < 0.0f) {
                    plan.glyph_ranges[glyph] = {start_frame, end_frame};
                    plan.enabled = true;
                }
            }
        }
        if (animator_color) plan.color = *animator_color;
    }
    return plan;
}

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
    std::span<const GpuTextGlyph> glyphs,
    float current_frame,
    const Color& highlight_color,
    bool highlight_enabled) {
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
        const auto pixels = glyph.pixels();
        const std::size_t expected =
            static_cast<std::size_t>(glyph.width) * glyph.height * 4;
        if (glyph.width == 0 || glyph.height == 0 ||
            pixels.size() != expected) {
            return graph::RenderOpResult(graph::RenderBackendError{
                graph::RenderBackendErrorCode::InvalidInput,
                "draw_packed_text_run: glyph rgba size mismatch"});
        }
    }

    std::vector<runtime::PackedGlyphBitmap> bitmap_views;
    bitmap_views.reserve(glyphs.size());
    for (const auto& glyph : glyphs) {
        const auto pixels = glyph.pixels();
        bitmap_views.push_back(runtime::PackedGlyphBitmap{
            glyph.width, glyph.height,
            pixels});
    }

    runtime::PackedTextAtlas persistent_atlas;
    std::string atlas_identity;
    bool stable_identity_valid = true;
    for (const auto& glyph : glyphs) {
        if (glyph.atlas_key.empty()) stable_identity_valid = false;
        const auto key_size = static_cast<std::uint32_t>(glyph.atlas_key.size());
        atlas_identity.append(reinterpret_cast<const char*>(&key_size), sizeof(key_size));
        atlas_identity.append(glyph.atlas_key);
        atlas_identity.append(reinterpret_cast<const char*>(&glyph.width), sizeof(glyph.width));
        atlas_identity.append(reinterpret_cast<const char*>(&glyph.height), sizeof(glyph.height));
    }
    if (!stable_identity_valid) atlas_identity.clear();
    const bool persistent_available = ctx.services.gpu_text_atlas_cache &&
        ctx.services.gpu_text_atlas_cache->acquire(
            bitmap_views, persistent_atlas, atlas_identity);

    std::vector<float> atlas_buffer;
    std::vector<runtime::GlyphInstance> instances;
    instances.reserve(glyphs.size());
    runtime::SurfaceDesc atlas_desc{};
    runtime::RenderSurfaceHandle atlas_handle = runtime::kInvalidRenderSurfaceHandle;
    bool cached_atlas = false;

    if (persistent_available) {
        // The runtime-owned cache has already done the shelf packing and owns
        // the device-local image through GpuAssetCache. Only the small
        // per-frame instance records are rebuilt here.
        atlas_handle = persistent_atlas.handle;
        cached_atlas = true;
        atlas_desc = runtime::SurfaceDesc{
            persistent_atlas.width, persistent_atlas.height,
            runtime::PixelFormat::Rgba32Float, runtime::ResourceUsage::Storage,
            runtime::LifetimeClass::JobPersistent, 0};
        for (std::size_t i = 0; i < glyphs.size(); ++i) {
            const auto& glyph = glyphs[i];
            instances.push_back(runtime::GlyphInstance{
                glyph.dst_x, glyph.dst_y,
                static_cast<std::int32_t>(persistent_atlas.origin_x[i]),
                static_cast<std::int32_t>(persistent_atlas.origin_y[i]),
                static_cast<std::int32_t>(glyph.width),
                static_cast<std::int32_t>(glyph.height),
                glyph.opacity, glyph.scale_x, glyph.scale_y, 0.0f,
                glyph.highlight_start_frame, glyph.highlight_end_frame});
        }
    } else {
        const auto [packed, dims] = pack_glyphs(glyphs);

        // Build the packed atlas buffer and per-glyph instances in one pass.
        atlas_buffer.assign(
            static_cast<std::size_t>(dims.width) * dims.height * 4, 0.0f);
        atlas_desc = runtime::SurfaceDesc{
            dims.width, dims.height, runtime::PixelFormat::Rgba32Float,
            runtime::ResourceUsage::Storage,
            ctx.services.gpu_asset_cache
                ? runtime::LifetimeClass::JobPersistent
                : runtime::LifetimeClass::FrameTransient,
            0};
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
                glyph.opacity, glyph.scale_x, glyph.scale_y, 0.0f});
        }
    }

    // Keep cache-hit and fallback costs separate. A persistent-atlas hit must
    // not appear as a CPU repack or an atlas upload in telemetry.
    if (profiling::g_current_counters &&
        (!persistent_available || !persistent_atlas.cache_hit)) {
        const auto repack_bytes = atlas_buffer.empty()
            ? static_cast<std::uint64_t>(atlas_desc.width) * atlas_desc.height * sizeof(float) * 4
            : static_cast<std::uint64_t>(atlas_buffer.size() * sizeof(float));
        profiling::g_current_counters->gpu_text_atlas_repack_count.fetch_add(
            1, std::memory_order_relaxed);
        profiling::g_current_counters->gpu_text_atlas_repack_bytes.fetch_add(
            repack_bytes,
            std::memory_order_relaxed);
    }
    if (profiling::g_current_counters && persistent_available &&
        persistent_atlas.uploaded) {
        profiling::g_current_counters->gpu_text_atlas_upload_count.fetch_add(
            1, std::memory_order_relaxed);
        profiling::g_current_counters->gpu_text_atlas_upload_bytes.fetch_add(
            static_cast<std::uint64_t>(atlas_desc.width) * atlas_desc.height * sizeof(float) * 4,
            std::memory_order_relaxed);
    }

    // Keep the packed atlas in the runtime-owned asset cache when available.
    // The bitmap bytes are the canonical identity: equal glyph pixels share
    // one device-local surface across frames and warm daemon jobs.
    if (!cached_atlas && ctx.services.gpu_asset_cache) {
        const std::string_view bytes(
            reinterpret_cast<const char*>(atlas_buffer.data()),
            atlas_buffer.size() * sizeof(float));
        runtime::GpuAssetKey key{
            assets::sha256_string(bytes),
            runtime::PixelFormat::Rgba32Float,
            atlas_desc.width,
            atlas_desc.height};
        const auto acquired = ctx.services.gpu_asset_cache->acquire(
            key, atlas_desc, atlas_buffer);
        atlas_handle = acquired.handle;
        cached_atlas = acquired.ok();
        if (!cached_atlas) {
            return graph::RenderOpResult(graph::RenderBackendError{
                graph::RenderBackendErrorCode::ExecutionFailure,
                "draw_packed_text_run: GPU atlas cache: " + acquired.error});
        }
    } else if (!cached_atlas) {
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
        if (profiling::g_current_counters) {
            profiling::g_current_counters->gpu_text_atlas_upload_count.fetch_add(
                1, std::memory_order_relaxed);
            profiling::g_current_counters->gpu_text_atlas_upload_bytes.fetch_add(
                static_cast<std::uint64_t>(atlas_buffer.size() * sizeof(float)),
                std::memory_order_relaxed);
        }
    }

    auto drawn = ctx.services.backend->draw_text_run_surface_timed(
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
    std::span<const GpuTextGlyph> glyphs,
    float current_frame,
    const Color& highlight_color,
    bool highlight_enabled) {
    if (!ensure_native_surface(ctx, destination)) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::UnsupportedCapability,
            "draw_packed_text_run: destination has no native surface support"});
    }
    auto result = draw_packed_text_run_surface(
        ctx, destination.surface_handle(), glyphs, current_frame,
        highlight_color, highlight_enabled);
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
    const auto highlight_plan = build_timed_highlight_plan(shape);
    const float current_frame = static_cast<float>(ctx.frame_input.sample_time.frame);
    std::vector<GpuTextGlyph> glyphs;
    glyphs.reserve(layout.placed.glyphs.size());
    for (std::size_t i = 0; i < layout.placed.glyphs.size(); ++i) {
        const auto& state = shape.glyphs[i];
        // HarfBuzz uses glyph id 0 for whitespace/missing-glyph entries.
        // They have no bitmap atlas entry and must not invalidate an
        // otherwise fully resident run.
        if (state.glyph_id == 0) continue;
        const auto& placed = layout.placed.glyphs[i];
        // FreeType's vertical metrics are positive-up: bbox_y1 is normally
        // smaller than bbox_y0 in screen coordinates.  The bitmap dimensions
        // below, not the signed metric ordering, determine whether there is
        // drawable ink.
        if (placed.bbox_x1 <= placed.bbox_x0) continue;
        if (!(state.scale.x > 0.0f) || !(state.scale.y > 0.0f) ||
            std::abs(state.rotation.x) > 1e-4f ||
            std::abs(state.rotation.y) > 1e-4f ||
            std::abs(state.rotation.z) > 1e-4f ||
            std::abs(state.skew) > 1e-4f || state.blur > 1e-4f) {
            return graph::RenderOpResult(graph::RenderBackendError{
                graph::RenderBackendErrorCode::UnsupportedCapability,
                "draw_cached_text_run: animated/style feature requires legacy text path"});
        }
        auto entry = ctx.services.text_render_resources->lookup_glyph_atlas(
            layout.font.font_path, state.glyph_id,
            static_cast<u32>(font_size));
        if ((!entry || !entry->image || entry->image->empty()) &&
            ctx.services.asset_resolver) {
            auto warmed = rasterize_missing_glyph(
                *ctx.services.text_render_resources, *ctx.services.asset_resolver,
                layout.font.font_path, state.glyph_id,
                static_cast<u32>(font_size), state.fill);
            if (warmed) {
                ctx.services.text_render_resources->store_glyph_atlas(
                    layout.font.font_path, state.glyph_id,
                    static_cast<u32>(font_size), *warmed);
                entry = std::move(warmed);
            }
        }
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
        const int stroke_radius = state.stroke.a > 1e-4f
            ? static_cast<int>(std::ceil(std::max(0.0f, state.stroke_width))) : 0;
        const int background_padding = state.background.a > 1e-4f ? 8 : 0;
        int shadow_padding = 0;
        for (const auto& shadow : shape.shadows) {
            if (!shadow.enabled || shadow.opacity <= 1e-4f) continue;
            shadow_padding = std::max(shadow_padding, static_cast<int>(std::ceil(
                std::max(std::abs(shadow.offset.x), std::abs(shadow.offset.y)) +
                std::max(0.0f, shadow.blur) + 1.0f)));
        }
        const int pad = std::max({stroke_radius, background_padding, shadow_padding});
        const int output_width = data.size.w + pad * 2;
        const int output_height = data.size.h + pad * 2;
        const Color raster_fill = highlight_plan.enabled ? shape.paint.fill : state.fill;
        std::string style_key;
        style_key.reserve(layout.font.font_path.size() + 128);
        style_key.append(layout.font.font_path);
        const auto append_key = [&](const auto& value) {
            style_key.append(reinterpret_cast<const char*>(&value), sizeof(value));
        };
        append_key(state.glyph_id);
        append_key(font_size);
        append_key(stroke_radius);
        append_key(background_padding);
        append_key(raster_fill);
        append_key(state.background);
        append_key(state.stroke);
        append_key(state.stroke_width);
        for (const auto& shadow : shape.shadows) {
            append_key(shadow.enabled);
            append_key(shadow.offset);
            append_key(shadow.blur);
            append_key(shadow.opacity);
            append_key(shadow.color);
        }
        auto styled = ctx.services.gpu_text_atlas_cache
            ? ctx.services.gpu_text_atlas_cache->find_styled(style_key)
            : std::shared_ptr<const runtime::GpuTextAtlasCache::StyledGlyphBitmap>{};
        if (profiling::g_current_counters) {
            auto& counter = styled
                ? profiling::g_current_counters->gpu_text_styled_cache_hits
                : profiling::g_current_counters->gpu_text_styled_cache_misses;
            counter.fetch_add(1, std::memory_order_relaxed);
        }
        if (!styled) {
        std::vector<float> rgba(static_cast<std::size_t>(output_width) * output_height * 4, 0.0f);
        auto source_alpha = [&](int x, int y) {
            if (x < 0 || y < 0 || x >= data.size.w || y >= data.size.h) return 0.0f;
            const auto* source_row = static_cast<const std::uint8_t*>(data.pixelData) +
                static_cast<std::size_t>(y) * data.stride;
            return source_row[static_cast<std::size_t>(x) * 4 + 3] / 255.0f;
        };
        auto over = [](float& dr, float& dg, float& db, float& da,
                       float sr, float sg, float sb, float sa) {
            dr = sr + dr * (1.0f - sa);
            dg = sg + dg * (1.0f - sa);
            db = sb + db * (1.0f - sa);
            da = sa + da * (1.0f - sa);
        };
        auto shadow_alpha = [&](const TextShadow& shadow, int x, int y) {
            const int sx = x - static_cast<int>(std::lround(shadow.offset.x));
            const int sy = y - static_cast<int>(std::lround(shadow.offset.y));
            const int radius = static_cast<int>(std::ceil(std::max(0.0f, shadow.blur)));
            if (radius <= 0) return source_alpha(sx, sy);
            float sum = 0.0f;
            int samples = 0;
            for (int oy = -1; oy <= 1; ++oy) {
                for (int ox = -1; ox <= 1; ++ox) {
                    sum += source_alpha(sx + ox * radius, sy + oy * radius);
                    ++samples;
                }
            }
            return sum / static_cast<float>(samples);
        };
        for (int y = 0; y < output_height; ++y) {
            const int source_y = y - pad;
            for (int x = 0; x < output_width; ++x) {
                const int source_x = x - pad;
                const float mask = source_alpha(source_x, source_y);
                float stroke_mask = 0.0f;
                if (stroke_radius > 0) {
                    for (int oy = -stroke_radius; oy <= stroke_radius; ++oy) {
                        for (int ox = -stroke_radius; ox <= stroke_radius; ++ox) {
                            if (ox * ox + oy * oy <= stroke_radius * stroke_radius) {
                                stroke_mask = std::max(stroke_mask,
                                    source_alpha(source_x + ox, source_y + oy));
                            }
                        }
                    }
                }
                const std::size_t off = (static_cast<std::size_t>(y) * output_width + x) * 4;
                float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
                if (state.background.a > 1e-4f) {
                    const float ba = std::clamp(state.background.a, 0.0f, 1.0f);
                    over(r, g, b, a, state.background.r * ba,
                         state.background.g * ba, state.background.b * ba, ba);
                }
                for (const auto& shadow : shape.shadows) {
                    if (!shadow.enabled || shadow.opacity <= 1e-4f) continue;
                    const float sa = shadow_alpha(shadow, source_x, source_y) *
                        std::clamp(shadow.opacity, 0.0f, 1.0f);
                    over(r, g, b, a, shadow.color.r * sa,
                         shadow.color.g * sa, shadow.color.b * sa, sa);
                }
                if (stroke_mask > 1e-4f && state.stroke.a > 1e-4f) {
                    const float sa = stroke_mask * std::clamp(state.stroke.a, 0.0f, 1.0f);
                    over(r, g, b, a, state.stroke.r * sa,
                         state.stroke.g * sa, state.stroke.b * sa, sa);
                }
                if (mask > 1e-4f) {
                    const float fa = mask * std::clamp(raster_fill.a, 0.0f, 1.0f);
                    over(r, g, b, a, raster_fill.r * fa,
                         raster_fill.g * fa, raster_fill.b * fa, fa);
                }
                rgba[off + 0] = r;
                rgba[off + 1] = g;
                rgba[off + 2] = b;
                rgba[off + 3] = a;
            }
        }
        auto pixels = std::make_shared<const std::vector<float>>(std::move(rgba));
        if (ctx.services.gpu_text_atlas_cache) {
            ctx.services.gpu_text_atlas_cache->store_styled(
                style_key, static_cast<std::uint32_t>(output_width),
                static_cast<std::uint32_t>(output_height), pixels);
        }
        styled = std::make_shared<const runtime::GpuTextAtlasCache::StyledGlyphBitmap>(
            runtime::GpuTextAtlasCache::StyledGlyphBitmap{
                static_cast<std::uint32_t>(output_width),
                static_cast<std::uint32_t>(output_height), std::move(pixels)});
        }
        if (!styled || !styled->rgba) {
            return graph::RenderOpResult(graph::RenderBackendError{
                graph::RenderBackendErrorCode::ExecutionFailure,
                "draw_cached_text_run: styled glyph cache returned no pixels"});
        }
        glyphs.push_back(GpuTextGlyph{
            styled->width,
            styled->height,
            {},
            static_cast<std::int32_t>(std::lround(model_matrix[3][0] +
                placed.x + state.position.x + entry->x_offset - pad)),
            static_cast<std::int32_t>(std::lround(model_matrix[3][1] +
                placed.y + state.position.y + entry->y_offset - pad)),
            state.scale.x,
            state.scale.y,
            state.opacity * opacity,
            styled->rgba,
            highlight_plan.enabled ? highlight_plan.glyph_ranges[i].first : -1.0f,
            highlight_plan.enabled ? highlight_plan.glyph_ranges[i].second : -1.0f});
        glyphs.back().atlas_key = std::move(style_key);
    }
    if (!ensure_native_surface(ctx, destination)) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::UnsupportedCapability,
            "draw_cached_text_run: destination has no native surface"});
    }

    auto result = draw_packed_text_run_surface(
        ctx, destination.surface_handle(), glyphs, current_frame,
        highlight_plan.color, highlight_plan.enabled);
    return result;
}

} // namespace chronon3d::graph::text_run
