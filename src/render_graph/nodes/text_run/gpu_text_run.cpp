// SPDX-License-Identifier: MIT
//
// gpu_text_run.cpp — packed-atlas builder for the GPU text-run fast path.
// See gpu_text_run.hpp for the contract.

#include "gpu_text_run.hpp"

#include "../native_surface.hpp"

#include <chronon3d/runtime/render_surface.hpp>
#include <chronon3d/runtime/gpu_asset_cache.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/runtime/gpu_glyph_atlas.hpp>
#include <chronon3d/text/mtsdf_preparation.hpp>
#include <chronon3d/text/mtsdf_profile.hpp>
#include <chronon3d/backends/text/text_render_resources.hpp>
#include <chronon3d/text/glyph_atlas.hpp>
#include <chronon3d/text/glyph_selector.hpp>
#include <chronon3d/text/animation/text_animator_properties.hpp>
#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <blend2d.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cmath>
#include <exception>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

namespace chronon3d::graph::text_run {

namespace {

// Defined with the rest of the text-run packing helpers below.  The run-level
// animation lowering lives above those helpers so it can also be used by the
// fused executor without duplicating the color packing contract.
std::uint32_t pack_text_color(const Color& color) noexcept;

bool is_full_run_selector(const TextRunShape& shape,
                          const TextAnimatorSpec& animator) noexcept {
    if (!shape.layout || shape.layout->units.word_count != 1 ||
        animator.selectors.size() != 1) {
        return false;
    }
    const auto& selector = animator.selectors.front();
    return selector.unit == TextSelectorUnit::Word &&
           (selector.shape == TextSelectorShape::Square ||
            selector.shape == TextSelectorShape::Smooth) &&
           selector.order == TextSelectorOrder::Forward &&
           selector.combine == SelectorCombineMode::Replace &&
           !selector.randomize_order && !selector.offset.is_time_dependent() &&
           !selector.start.has_expression() &&
           !selector.end.has_expression() &&
           !selector.amount.has_expression() &&
           std::abs(selector.start.evaluate(0.0)) <= 1e-6f &&
           std::abs(selector.end.evaluate(0.0) - 100.0f) <= 1e-6f;
}

float sample_full_run_selector_weight(
    const GlyphSelectorSpec& selector,
    SampleTime sample_time) noexcept {
    const float start = selector.start.evaluate(sample_time) * 0.01f;
    const float end = selector.end.evaluate(sample_time) * 0.01f;
    const float offset = selector.offset.evaluate(sample_time) * 0.01f;
    float position = 0.5f - offset;
    position -= std::floor(position);
    const float shape_weight = detail::evaluate_selector_shape(
        selector.shape, position, start, end);
    return std::clamp(
        shape_weight * selector.amount.evaluate(sample_time) * 0.01f,
        0.0f, 1.0f);
}

} // namespace

bool can_lower_gpu_run_level_animation(const TextRunShape& shape) noexcept {
    if (!shape.layout || shape.animated_doc || shape.animators.size() != 1) {
        return false;
    }
    const auto& animator = shape.animators.front();
    if (!animator.enabled || !is_full_run_selector(shape, animator)) return false;

    bool saw_dynamic_property = false;
    for (const auto& property : animator.properties) {
        if (const auto* position = std::get_if<PositionProperty>(&property)) {
            if (position->value.has_expression()) return false;
            saw_dynamic_property = true;
        } else if (const auto* scale = std::get_if<ScaleProperty>(&property)) {
            if (scale->value.has_expression()) return false;
            saw_dynamic_property = true;
        } else if (const auto* opacity = std::get_if<OpacityProperty>(&property)) {
            if (opacity->value.has_expression()) return false;
            saw_dynamic_property = true;
        } else if (std::holds_alternative<FillColorProperty>(property)) {
            saw_dynamic_property = true;
        } else {
            return false;
        }
    }
    return saw_dynamic_property;
}

void sample_gpu_run_level_animation(
    const TextRunShape& shape,
    SampleTime sample_time,
    runtime::TextRunDynamic& run) noexcept {
    if (!can_lower_gpu_run_level_animation(shape)) return;
    const auto& animator = shape.animators.front();
    const auto& selector = animator.selectors.front();
    const float selector_weight =
        sample_full_run_selector_weight(selector, sample_time);
    for (const auto& property : animator.properties) {
        if (const auto* position = std::get_if<PositionProperty>(&property)) {
            const auto value = position->value.evaluate(sample_time);
            run.tx += value.x * selector_weight;
            run.ty += value.y * selector_weight;
        } else if (const auto* scale = std::get_if<ScaleProperty>(&property)) {
            const auto value = scale->value.evaluate(sample_time);
            run.sx *= 1.0f + (value.x - 1.0f) * selector_weight;
            run.sy *= 1.0f + (value.y - 1.0f) * selector_weight;
        } else if (const auto* opacity = std::get_if<OpacityProperty>(&property)) {
            const auto value = opacity->value.evaluate(sample_time);
            run.opacity *= 1.0f + (value - 1.0f) * selector_weight;
        } else if (const auto* fill = std::get_if<FillColorProperty>(&property)) {
            const auto unpack = [](std::uint32_t packed, int shift) {
                return static_cast<float>((packed >> shift) & 0xFFu) / 255.0f;
            };
            Color base{
                unpack(run.color, 0), unpack(run.color, 8),
                unpack(run.color, 16), unpack(run.color, 24)};
            base.r += (fill->color.r - base.r) * selector_weight;
            base.g += (fill->color.g - base.g) * selector_weight;
            base.b += (fill->color.b - base.b) * selector_weight;
            base.a += (fill->color.a - base.a) * selector_weight;
            run.color = pack_text_color(base);
        }
    }
}

bool can_lower_gpu_text_animation(const TextRunShape& shape) noexcept {
    if (!shape.layout || shape.animated_doc ||
        shape.layout->font_spans.size() > 1 ||
        shape.paint.fill_style || shape.paint.stroke_style ||
        shape.material.enabled || shape.shadows.size() > 1 ||
        shape.glyphs.size() != shape.layout->placed.glyphs.size()) {
        return false;
    }
    if (shape.paint.stroke_enabled &&
        (shape.paint.stroke_width < 0.0f || shape.paint.stroke_width > 16.0f)) {
        return false;
    }
    for (const auto& shadow : shape.shadows) {
        if (!shadow.enabled) continue;
        if (shadow.blur < 0.0f || shadow.blur > 64.0f ||
            std::abs(shadow.offset.x) + shadow.blur > 64.0f ||
            std::abs(shadow.offset.y) + shadow.blur > 64.0f) {
            return false;
        }
    }
    if (shape.animators.empty()) return true;
    if (shape.animators.size() != 1) return false;
    const auto& animator = shape.animators.front();
    if (!animator.enabled || animator.selectors.size() != 1 ||
        animator.properties.empty()) {
        return false;
    }
    if (can_lower_gpu_run_level_animation(shape)) return true;
    const auto& selector = animator.selectors.front();
    // This is the canonical per-glyph GPU text contract. The whole shaped
    // run remains resident; only this small dynamic record changes per frame.
    if ((selector.unit != TextSelectorUnit::Glyph &&
         selector.unit != TextSelectorUnit::Character &&
         selector.unit != TextSelectorUnit::Grapheme) ||
        (selector.shape != TextSelectorShape::Square &&
         selector.shape != TextSelectorShape::Smooth) ||
        selector.order != TextSelectorOrder::Forward ||
        selector.combine != SelectorCombineMode::Replace ||
        selector.randomize_order || selector.offset.is_time_dependent() ||
        std::abs(selector.offset.evaluate(0.0)) > 1e-6f ||
        std::abs(selector.amount.evaluate(0.0) - 100.0f) > 1e-6f) {
        return false;
    }
    bool saw_supported_property = false;
    for (const auto& property : animator.properties) {
        if (const auto* opacity = std::get_if<OpacityProperty>(&property)) {
            if (opacity->value.has_expression()) return false;
            saw_supported_property = true;
        } else if (const auto* position = std::get_if<PositionProperty>(&property)) {
            if (position->value.has_expression()) return false;
            saw_supported_property = true;
        } else if (const auto* scale = std::get_if<ScaleProperty>(&property)) {
            if (scale->value.has_expression()) return false;
            saw_supported_property = true;
        } else if (const auto* tracking = std::get_if<TrackingProperty>(&property)) {
            if (tracking->pixels.has_expression()) return false;
            saw_supported_property = true;
        } else if (const auto* blur = std::get_if<BlurProperty>(&property)) {
            if (blur->radius.has_expression()) return false;
            saw_supported_property = true;
        } else {
            return false;
        }
    }
    return saw_supported_property;
}

void sample_gpu_text_animation(
    const TextRunShape& shape,
    SampleTime sample_time,
    runtime::TextRunDynamic& run) noexcept {
    if (!shape.layout || shape.animators.size() != 1 ||
        !can_lower_gpu_text_animation(shape)) return;
    const auto& animator = shape.animators.front();
    if (animator.selectors.size() != 1) return;
    const auto& selector = animator.selectors.front();
    if (selector.unit != TextSelectorUnit::Glyph &&
        selector.unit != TextSelectorUnit::Character &&
        selector.unit != TextSelectorUnit::Grapheme) return;

    // flags bit 0 is shaped-plane placement; bits 1/2 are the dynamic
    // selector enable/shape contract shared with text_tile_bin/raster.
    run.flags |= 2u;
    if (selector.shape == TextSelectorShape::Smooth) run.flags |= 4u;
    run.visible_glyph_count = std::numeric_limits<std::uint32_t>::max();
    run.selector_start = selector.start.evaluate(sample_time);
    run.selector_end = selector.end.evaluate(sample_time);
    run.selector_amount = selector.amount.evaluate(sample_time);
    run.selector_unit_count = shape.layout->units.unit_count(selector.unit);

    for (const auto& property : animator.properties) {
        if (const auto* opacity = std::get_if<OpacityProperty>(&property)) {
            run.animator_opacity *= opacity->value.evaluate(sample_time);
        } else if (const auto* position = std::get_if<PositionProperty>(&property)) {
            const auto value = position->value.evaluate(sample_time);
            run.animator_position_x += value.x;
            run.animator_position_y += value.y;
        } else if (const auto* scale = std::get_if<ScaleProperty>(&property)) {
            const auto value = scale->value.evaluate(sample_time);
            run.animator_scale_x *= value.x;
            run.animator_scale_y *= value.y;
        } else if (const auto* tracking = std::get_if<TrackingProperty>(&property)) {
            run.animator_tracking += tracking->pixels.evaluate(sample_time);
        } else if (const auto* blur = std::get_if<BlurProperty>(&property)) {
            run.animator_blur += std::max(0.0f, blur->radius.evaluate(sample_time));
        }
    }
}

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

std::uint32_t pack_text_color(const Color& color) noexcept {
    const auto to_byte = [](float value) {
        return static_cast<std::uint32_t>(std::clamp(
            std::lround(value * 255.0f), 0L, 255L));
    };
    return to_byte(color.r) | (to_byte(color.g) << 8) |
           (to_byte(color.b) << 16) | (to_byte(color.a) << 24);
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
    if (!handle.valid()) {
        spdlog::error("[rasterize_missing_glyph] handle not valid for font_path='{}'", font_path);
        return std::nullopt;
    }
    BLFont font;
    if (font.createFromFace(*handle.bl_face, static_cast<float>(font_size)) != BL_SUCCESS) {
        spdlog::error("[rasterize_missing_glyph] createFromFace failed for font_path='{}'", font_path);
        return std::nullopt;
    }
    BLBoxI bounds{};
    if (font.getGlyphBounds(&glyph_id, 0, &bounds, 1) != BL_SUCCESS) return std::nullopt;
    constexpr double kGlyphUnit = 1.0 / 64.0;
    const double bx0 = static_cast<double>(bounds.x0) * kGlyphUnit;
    const double by0 = static_cast<double>(bounds.y0) * kGlyphUnit;
    const double bx1 = static_cast<double>(bounds.x1) * kGlyphUnit;
    const double by1 = static_cast<double>(bounds.y1) * kGlyphUnit;
    const int x0 = static_cast<int>(std::floor(bx0));
    const int y0 = static_cast<int>(std::floor(by0));
    const int width = static_cast<int>(std::ceil(bx1)) - x0;
    const int height = static_cast<int>(std::ceil(by1)) - y0;
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
            !checked_mul(static_cast<std::size_t>(glyph.width),
                         static_cast<std::size_t>(glyph.height), quad) ||
            !checked_mul(quad, 4u, expected) ||
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
        const auto [packed, dims] = pack_glyphs(glyphs);
        if (profiling::g_current_counters) {
            profiling::g_current_counters->gpu_text_atlas_pack_wall_us.fetch_add(
                static_cast<std::uint64_t>(profiling::elapsed_us(pack_start)),
                std::memory_order_relaxed);
        }

        // Build the packed atlas buffer and per-glyph instances in one pass.
        std::size_t atlas_pixels = 0;
        std::size_t atlas_bytes = 0;
        if (!checked_mul(static_cast<std::size_t>(dims.width),
                         static_cast<std::size_t>(dims.height), atlas_pixels) ||
            !checked_mul(atlas_pixels, 4u, atlas_bytes)) {
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
            !checked_mul(static_cast<std::size_t>(glyph.width),
                         static_cast<std::size_t>(glyph.height), quad) ||
            !checked_mul(quad, 4u, expected) ||
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

std::shared_ptr<const runtime::GpuPreparedTextRun> prepare_mtsdf_text_run(
    runtime::GpuGlyphAtlas& glyph_atlas,
    const assets::AssetResolver* resolver,
    const TextRunShape& shape) {
    if (!shape.layout ||
        shape.animated_doc ||
        shape.layout->font_spans.empty() || shape.paint.fill_style ||
        shape.paint.stroke_style || shape.material.enabled ||
        shape.shadows.size() > 1 ||
        shape.glyphs.size() != shape.layout->placed.glyphs.size()) {
        return nullptr;
    }
    if (shape.paint.stroke_enabled &&
        (shape.paint.stroke_width < 0.0f || shape.paint.stroke_width > 16.0f)) {
        return nullptr;
    }
    for (const auto& shadow : shape.shadows) {
        if (!shadow.enabled) continue;
        if (shadow.blur < 0.0f || shadow.blur > 64.0f ||
            std::abs(shadow.offset.x) + shadow.blur > 64.0f ||
            std::abs(shadow.offset.y) + shadow.blur > 64.0f) {
            return nullptr;
        }
    }

    constexpr auto profile_spec = text::kCanonicalMtsdfProfile;
    constexpr std::uint16_t kEmPx = profile_spec.em_px;
    constexpr std::uint16_t kRangePx = profile_spec.distance_range_px;
    constexpr std::uint16_t kPaddingPx = profile_spec.padding_px;
    constexpr std::uint16_t kProfileId = profile_spec.generation_id;
    const runtime::DistanceFieldProfile profile{
        runtime::GlyphRepresentation::Mtsdf, kEmPx, kRangePx, kPaddingPx};
    const auto& layout = *shape.layout;
    const float layout_scale = std::max(1.0f, layout.font_size) /
        static_cast<float>(kEmPx);

    std::uint32_t covered_glyphs = 0;
    for (const auto& span : layout.font_spans) {
        if (span.glyph_begin != covered_glyphs ||
            span.glyph_end < span.glyph_begin ||
            span.glyph_end > layout.placed.glyphs.size()) {
            return nullptr;
        }
        covered_glyphs = span.glyph_end;
    }
    if (covered_glyphs != layout.placed.glyphs.size()) {
        return nullptr;
    }

    auto prepared = std::make_shared<runtime::GpuPreparedTextRun>();

    if (!shape.animators.empty()) {
        if (shape.animators.front().selectors.empty()) {
            return nullptr;
        }
        const auto& selector_spec = shape.animators.front().selectors.front();
        prepared->reveal_selector = compile_selector(
            selector_spec,
            layout.units, layout.source_text, &layout.placed);
    }
    if (!resolver) return nullptr;

    std::vector<runtime::GpuGlyphKey> keys;
    // drawable_layout[i] is the layout index of keys[i]. It is recorded in the
    // SAME pass that appends the key, so the batch-residency loop below never
    // rescans the layout to recover it. The rescan was quadratic: for every
    // key it walked layout.placed.glyphs from index 0 counting drawable glyphs,
    // i.e. O(glyphs^2) per prepared run (a few thousand glyphs per frame on an
    // animated text run).
    std::vector<std::size_t> drawable_layout;
    std::vector<std::uint32_t> widths;
    std::vector<std::uint32_t> heights;
    std::vector<runtime::GpuGlyphMetrics> metrics;
    std::vector<runtime::GpuGlyphKey> pending_keys;
    std::vector<std::uint32_t> pending_widths;
    std::vector<std::uint32_t> pending_heights;
    std::vector<runtime::GpuGlyphMetrics> pending_metrics;
    std::vector<float> pending_pixels;
    keys.reserve(layout.placed.glyphs.size());
    drawable_layout.reserve(layout.placed.glyphs.size());
    widths.reserve(layout.placed.glyphs.size());
    heights.reserve(layout.placed.glyphs.size());
    metrics.reserve(layout.placed.glyphs.size());
    pending_keys.reserve(layout.placed.glyphs.size());
    pending_widths.reserve(layout.placed.glyphs.size());
    pending_heights.reserve(layout.placed.glyphs.size());
    pending_metrics.reserve(layout.placed.glyphs.size());

    std::size_t span_index = 0;
    for (std::size_t i = 0; i < layout.placed.glyphs.size(); ++i) {
        while (span_index + 1 < layout.font_spans.size() &&
               i >= layout.font_spans[span_index].glyph_end) {
            ++span_index;
        }
        const auto& state = shape.glyphs[i];
        const auto& placed = layout.placed.glyphs[i];
        if (state.glyph_id == 0 || placed.bbox_x1 <= placed.bbox_x0) continue;
        const auto key = runtime::GpuGlyphKey::make(
            layout.font_spans[span_index].font.font_path, state.glyph_id, 0,
            runtime::GlyphRepresentation::Mtsdf, kProfileId, 0);
        keys.push_back(key);
        drawable_layout.push_back(i);
        // One shared_lock for the resident location AND its metrics: this used
        // to be two acquisitions (location() + metrics()) on the same mutex for
        // every glyph of the run.
        std::array<std::optional<runtime::GpuGlyphAtlas::Residency>, 1> resident_slot{};
        const std::array<runtime::GpuGlyphKey, 1> key_slot{key};
        glyph_atlas.resolve_resident(key_slot, resident_slot);
        if (resident_slot[0].has_value()) {
            const auto& resident = *resident_slot[0];
            widths.push_back(resident.location.width);
            heights.push_back(resident.location.height);
            metrics.push_back(resident.metrics.value_or(
                runtime::GpuGlyphMetrics{0, 0, placed.advance_x}));
            continue;
        }
        const auto generated = text::prepare_mtsdf_glyph(
            *resolver, layout.font_spans[span_index].font.font_path,
            state.glyph_id, profile_spec);
        if (!generated || generated->rgba8.empty()) {
            // A drawable glyph cannot be silently omitted: a partial batch is
            // visually non-deterministic and must not be marked GPU-ready.
            return nullptr;
        }
        const runtime::GpuGlyphMetrics glyph_metrics{
            generated->x_offset, generated->y_offset, generated->advance_x};
        widths.push_back(generated->width);
        heights.push_back(generated->height);
        metrics.push_back(glyph_metrics);
        pending_keys.push_back(key);
        pending_widths.push_back(generated->width);
        pending_heights.push_back(generated->height);
        pending_metrics.push_back(glyph_metrics);
        pending_pixels.insert(pending_pixels.end(), generated->rgba8.begin(),
                              generated->rgba8.end());
    }

    if (!pending_keys.empty()) {
        const auto uploaded = glyph_atlas.prepare_batch(
            pending_keys, pending_widths, pending_heights, pending_pixels,
            pending_metrics, profile);
        if (!uploaded.has_value()) {
            return nullptr;
        }
    }

    prepared->batches.reserve(1);
    // Batch residency for the whole run under ONE shared_lock: the per-glyph
    // location() + page_handle() pair cost two acquisitions per glyph per frame
    // on the same mutex, and the diagnostic run can carry thousands of glyphs.
    std::vector<std::optional<runtime::GpuGlyphAtlas::Residency>> residency(keys.size());
    glyph_atlas.resolve_resident(keys, residency);
    for (std::size_t key_index = 0; key_index < keys.size(); ++key_index) {
        const auto& resident = residency[key_index];
        if (!resident) continue;
        // An invalid page handle means the atlas no longer holds a live page
        // for this location (cleared underneath the run). Treat it as a miss,
        // exactly as the per-glyph page_handle() contract did.
        const auto page = resident->page;
        if (page == runtime::kInvalidRenderSurfaceHandle) continue;
        auto batch = std::find_if(
            prepared->batches.begin(), prepared->batches.end(),
            [page](const runtime::GpuPreparedTextRun::Batch& candidate) {
                return candidate.page == page;
            });
        if (batch == prepared->batches.end()) {
            prepared->batches.push_back(runtime::GpuPreparedTextRun::Batch{});
            batch = std::prev(prepared->batches.end());
            batch->page = page;
            batch->glyphs.reserve(keys.size());
        }
        // O(1): the layout index was recorded when the key was appended, so
        // the former per-key rescan of layout.placed.glyphs (O(glyphs^2) per
        // prepared run) is gone.
        const std::size_t layout_index = drawable_layout[key_index];
        const auto& placed = layout.placed.glyphs[layout_index];
        const auto& location = resident->location;
        runtime::GlyphStatic glyph{};
        glyph.atlas_page = 0;
        glyph.flags = 1u;
        glyph.atlas_x = location.atlas_x;
        glyph.atlas_y = location.atlas_y;
        glyph.atlas_w = location.width;
        glyph.atlas_h = location.height;
        const float pad = static_cast<float>(kPaddingPx) * layout_scale;
        glyph.plane_left = placed.x + placed.bbox_x0 * layout_scale - pad;
        glyph.plane_top = placed.y - placed.bbox_y0 * layout_scale - pad;
        glyph.plane_right = glyph.plane_left +
            static_cast<float>(location.width) * layout_scale;
        glyph.plane_bottom = glyph.plane_top +
            static_cast<float>(location.height) * layout_scale;
        glyph.draw_order = static_cast<std::uint32_t>(layout_index);
        glyph.pad0 = static_cast<std::uint32_t>(layout_index);
        batch->glyphs.push_back(glyph);
    }
    prepared->ready = !prepared->batches.empty() &&
        std::any_of(prepared->batches.begin(), prepared->batches.end(),
                    [](const runtime::GpuPreparedTextRun::Batch& batch) {
                        return !batch.glyphs.empty() &&
                            batch.page != runtime::kInvalidRenderSurfaceHandle;
                    });
    return prepared;
}

// Canonical static text route. Keyed by the immutable shape layout in the
// runtime-owned GpuTextRunPreparedCache; every frame submits only GlyphStatic[]
// + one TextRunDynamic record.
graph::RenderOpResult draw_cached_text_run_mtsdf(
    RenderGraphContext& ctx,
    Framebuffer& destination,
    const TextRunShape& shape,
    const glm::mat4& model_matrix,
    float opacity) {
    if (!ctx.gpu_glyph_atlas || !ctx.gpu_text_prepared_cache ||
        !ctx.asset_resolver || !ctx.backend ||
        !ctx.surface_registry || !shape.layout) {
        return graph::RenderOpResult(graph::make_render_backend_error(
            graph::RenderBackendErrorCode::UnsupportedCapability,
            "MTSDF text route does not support this run"));
    }

    auto prepared = ctx.gpu_text_prepared_cache->find(*shape.layout);
    if (!prepared) {
        auto created = prepare_mtsdf_text_run(
            *ctx.gpu_glyph_atlas, ctx.asset_resolver, shape);
        if (!created || !created->ready) {
            return graph::RenderOpResult(graph::make_render_backend_error(
                graph::RenderBackendErrorCode::UnsupportedCapability,
                "MTSDF text route has no resident glyphs"));
        }
        ctx.gpu_text_prepared_cache->store(*shape.layout, created);
        prepared = created;
    }

    if (!prepared->ready) {
        return graph::RenderOpResult(graph::make_render_backend_error(
            graph::RenderBackendErrorCode::UnsupportedCapability,
            "MTSDF text route has no resident glyphs"));
    }

    const auto& layout = *shape.layout;
    constexpr std::uint16_t kRangePx = text::kCanonicalMtsdfProfile.distance_range_px;

    runtime::TextRunDynamic run{};
    run.tx = model_matrix[3][0];
    run.ty = model_matrix[3][1];
    run.sx = model_matrix[0][0];
    run.sy = model_matrix[1][1];
    run.opacity = opacity;
    run.color = pack_text_color(shape.paint.fill);
    // Keep the shader interpretation tied to the atlas profile rather than
    // duplicating the distance range as a magic constant in GLSL.
    run.distance_range_bits = std::bit_cast<std::uint32_t>(
        static_cast<float>(kRangePx));
    run.visible_glyph_count = std::numeric_limits<std::uint32_t>::max();
    // Lowerable glyph selectors stay entirely GPU-native.  The shader uses
    // the selector scalars populated below, so a CPU per-glyph visibility
    // pass here would be redundant and would reintroduce frame-loop work.
    if (prepared->reveal_selector &&
        !can_lower_gpu_text_animation(shape)) {
        std::uint32_t visible_limit = 0;
        const auto sample_time = ctx.frame_input.sample_time;
        for (std::size_t i = 0; i < layout.placed.glyphs.size(); ++i) {
            if (shape.glyphs[i].glyph_id == 0 ||
                layout.placed.glyphs[i].bbox_x1 <= layout.placed.glyphs[i].bbox_x0) {
                continue;
            }
            const auto weight = evaluate_compiled_selector(
                *prepared->reveal_selector, layout.units,
                static_cast<std::uint32_t>(i), sample_time);
            if (weight <= 1e-6f) {
                visible_limit = static_cast<std::uint32_t>(i + 1);
            }
        }
        run.visible_glyph_count = visible_limit;
    }
    sample_gpu_text_animation(shape, ctx.frame_input.sample_time, run);
    if (shape.paint.stroke_enabled && shape.paint.stroke_width > 0.0f) {
        run.effect_flags |= 1u;
        run.stroke_width = shape.paint.stroke_width;
        run.stroke_color = pack_text_color(shape.paint.stroke_color);
    }
    for (const auto& shadow : shape.shadows) {
        if (!shadow.enabled) continue;
        Color shadow_color = shadow.color;
        shadow_color.a = std::clamp(shadow_color.a * shadow.opacity, 0.0f, 1.0f);
        run.shadow_color = pack_text_color(shadow_color);
        run.shadow_dx = shadow.offset.x;
        run.shadow_dy = shadow.offset.y;
        run.shadow_blur = shadow.blur;
        run.effect_flags |= (std::abs(shadow.offset.x) < 1e-4f &&
                             std::abs(shadow.offset.y) < 1e-4f) ? 4u : 2u;
        break;
    }

    if (!ensure_empty_native_surface(ctx, destination)) {
        return graph::RenderOpResult(graph::make_render_backend_error(
            graph::RenderBackendErrorCode::UnsupportedCapability,
            "MTSDF text route destination has no native surface"));
    }
    const auto cleared = ctx.backend->fill_rect_surface(
        destination.surface_handle(), 0, 0, destination.width(), destination.height(),
        Color::transparent());
    if (!cleared.ok()) return cleared;
    std::size_t items_drawn = 0;
    for (const auto& batch : prepared->batches) {
        if (batch.glyphs.empty()) continue;
        const auto dispatch = ctx.backend->draw_text_batch(
            destination.surface_handle(), batch.glyphs,
            std::span<const runtime::TextRunDynamic>(&run, 1),
            std::span<const runtime::RenderSurfaceHandle>(&batch.page, 1));
        if (!dispatch.ok()) return dispatch;
        items_drawn += dispatch.value().items_drawn;
    }
    return graph::RenderOpResult(graph::RenderOpOutcome{items_drawn});
}

#include "gpu_text_run_cached.inc"

} // namespace chronon3d::graph::text_run
