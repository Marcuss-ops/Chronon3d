// SPDX-License-Identifier: MIT
//
// gpu_text_run.cpp — packed-atlas builder for the GPU text-run fast path.
// See gpu_text_run.hpp for the contract.

#include "gpu_text_run.hpp"

#include "../native_surface.hpp"

#include <chronon3d/runtime/render_surface.hpp>
#include <chronon3d/runtime/gpu_asset_cache.hpp>
#include <chronon3d/runtime/gpu_glyph_atlas.hpp>
#include <chronon3d/backends/text/text_render_resources.hpp>
#include <chronon3d/text/glyph_atlas.hpp>
#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <blend2d.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

namespace chronon3d::graph::text_run {

namespace {

/// Fixed packed-atlas width.  Glyphs are shelf-packed left-to-right and
/// wrapped to a new row when a glyph would overflow this width.
constexpr std::uint32_t kMaxAtlasWidth = 1024;

/// Overflow-safe `a * b`.  Returns false on overflow and never writes `out`.
/// Glyph dimensions flow in from layout/font/effect code and must never be
/// trusted to fit `size_t` when computing payload sizes.
bool checked_mul(std::size_t a, std::size_t b, std::size_t& out) {
    if (a != 0 && b > std::numeric_limits<std::size_t>::max() / a) {
        return false;
    }
    out = a * b;
    return true;
}

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
    const int x0 = bounds.x0;
    const int y0 = bounds.y0;
    const int x1 = bounds.x1;
    const int y1 = bounds.y1;
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
        static_cast<std::uint32_t>(std::clamp(color.r, 0.0f, 1.0f) * 255.0f),
        static_cast<std::uint32_t>(std::clamp(color.g, 0.0f, 1.0f) * 255.0f),
        static_cast<std::uint32_t>(std::clamp(color.b, 0.0f, 1.0f) * 255.0f),
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

    const auto glyph_count = shape.layout->placed.glyphs.size();
    if (shape.layout->units.glyph_to_word.size() < glyph_count) {
        spdlog::error(
            "[text-run] invalid glyph_to_word mapping: placed_glyphs={} glyph_to_word={}",
            glyph_count, shape.layout->units.glyph_to_word.size());
        return plan;
    }

    plan.glyph_ranges.assign(glyph_count, {-1.0f, -1.0f});
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
        // Bug fix on main: returning RenderOpOutcome{0} here caused the
        // upstream caller in text_run_execution.cpp:101-102 to treat this
        // path as success-with-zero-items and skip the legacy backend.draw_text_run()
        // fallback, which produced all-black frames for every preset-driven render.
        // Returning an UnsupportedCapability error forces the caller to fall
        // back to the legacy text path that actually draws pixels.
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::UnsupportedCapability,
            "draw_packed_text_run_surface: empty glyph vector; fall back to legacy text path"});
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
        std::size_t quad = 0;
        std::size_t expected = 0;
        if (glyph.width == 0 || glyph.height == 0 ||
            !checked_mul(static_cast<std::size_t>(glyph.width),
                         static_cast<std::size_t>(glyph.height), quad) ||
            !checked_mul(quad, 4u, expected) ||
            pixels.size() != expected) {
            return graph::RenderOpResult(graph::RenderBackendError{
                graph::RenderBackendErrorCode::InvalidInput,
                "draw_packed_text_run: glyph rgba size mismatch"});
        }
    }

    std::vector<float> atlas_buffer;
    std::vector<runtime::GlyphInstance> instances;
    instances.reserve(glyphs.size());
    runtime::SurfaceDesc atlas_desc{};
    runtime::RenderSurfaceHandle atlas_handle = runtime::kInvalidRenderSurfaceHandle;
    bool cached_atlas = false;

    {
        const auto [packed, dims] = pack_glyphs(glyphs);

        // Build the packed atlas buffer and per-glyph instances in one pass.
        std::size_t atlas_pixels = 0;
        std::size_t atlas_bytes = 0;
        if (!checked_mul(static_cast<std::size_t>(dims.width),
                         static_cast<std::size_t>(dims.height), atlas_pixels) ||
            !checked_mul(atlas_pixels, 4u, atlas_bytes)) {
            return graph::RenderOpResult(graph::RenderBackendError{
                graph::RenderBackendErrorCode::InvalidInput,
                "draw_packed_text_run_surface: atlas dimensions overflow"});
        }
        atlas_buffer.assign(atlas_bytes, 0.0f);
        atlas_desc = runtime::SurfaceDesc::make(
            dims.width, dims.height, runtime::PixelFormat::Rgba32Float,
            runtime::ResourceUsage::Storage,
            ctx.services.gpu_asset_cache
                ? runtime::LifetimeClass::JobPersistent
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
                glyph.opacity, glyph.scale_x, glyph.scale_y, 0.0f});
        }
    }

    // Keep fallback costs separate from retired persistent-atlas hits: a CPU
    // repack or atlas upload must never appear as a persistent cache hit in
    // telemetry.
    if (profiling::g_current_counters) {
        const auto repack_bytes = atlas_buffer.empty()
            ? static_cast<std::uint64_t>(atlas_desc.bytes)
            : static_cast<std::uint64_t>(atlas_buffer.size() * sizeof(float));
        profiling::g_current_counters->gpu_text_atlas_repack_count.fetch_add(
            1, std::memory_order_relaxed);
        profiling::g_current_counters->gpu_text_atlas_repack_bytes.fetch_add(
            repack_bytes,
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
        profiling::GpuUploadProducerScope upload_scope(
            profiling::GpuUploadProducer::Text);
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
            !checked_mul(static_cast<std::size_t>(glyph.width),
                         static_cast<std::size_t>(glyph.height), quad) ||
            !checked_mul(quad, 4u, expected) ||
            pixels.size() != expected) {
            return graph::RenderOpResult(graph::RenderBackendError{
                graph::RenderBackendErrorCode::InvalidInput,
                "draw_packed_text_run: glyph rgba size mismatch"});
        }
    }
    // TextRun output is an independent transparent surface. Avoid uploading
    // the CPU framebuffer before the glyph pass; this was a full-canvas
    // CPU->GPU transfer for every watermark/subtitle layer and frame.
    if (!ensure_empty_native_surface(ctx, destination)) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::UnsupportedCapability,
            "draw_packed_text_run: destination has no native surface support"});
    }
    const auto cleared = ctx.services.backend->fill_rect_surface(
        destination.surface_handle(), 0, 0, destination.width(), destination.height(),
        Color::transparent());
    if (!cleared.ok()) return cleared;
    auto result = draw_packed_text_run_surface(
        ctx, destination.surface_handle(), glyphs, current_frame,
        highlight_color, highlight_enabled);
    if (!result.ok()) release_native_surface(ctx, destination);
    return result;
}

#include "gpu_text_run_cached.inc"

} // namespace chronon3d::graph::text_run
