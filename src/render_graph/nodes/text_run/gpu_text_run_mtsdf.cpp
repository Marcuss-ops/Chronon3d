// SPDX-License-Identifier: MIT
//
// gpu_text_run_mtsdf.cpp — MTSDF preparation and cached dispatch for the GPU
// text-run family.
//
// Split out of gpu_text_run.cpp: it builds the persistent MTSDF glyph-atlas
// representation of a shaped run (prepare_mtsdf_text_run) and dispatches the
// already-prepared batches straight from the runtime prepared cache
// (draw_cached_text_run_mtsdf). See gpu_text_run.hpp for the contract.

#include "gpu_text_run.hpp"

#include "../native_surface.hpp"

#include <chronon3d/runtime/gpu_glyph_atlas.hpp>
#include <chronon3d/text/mtsdf_preparation.hpp>
#include <chronon3d/text/mtsdf_profile.hpp>
#include <chronon3d/text/glyph_selector.hpp>
#include <chronon3d/text/text_color_packing.hpp>
#include <chronon3d/assets/asset_resolver.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <vector>
#include <spdlog/spdlog.h>

namespace chronon3d::graph::text_run {

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
        // The prepared plane is in layout space.  Keep the baseline origin
        // beside it so a selector-weighted scale can be inverted around the
        // glyph's own layout origin in the MTSDF shader.
        glyph.origin_x = placed.x;
        glyph.origin_y = placed.y;
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
        spdlog::warn(
            "[text-route] frame={} route=mtsdf reason=missing-native-contract",
            ctx.frame_input.frame.integral());
        return graph::RenderOpResult(graph::make_render_backend_error(
            graph::RenderBackendErrorCode::UnsupportedCapability,
            "MTSDF text route does not support this run"));
    }
    // A GPU success must never silently drop an unsupported animator. The
    // canonical MTSDF route owns the lowerable typewriter selector; only
    // unsupported animator stacks are rejected here.
    if (!shape.animators.empty() && !can_lower_gpu_text_animation(shape)) {
        spdlog::warn(
            "[text-route] frame={} route=mtsdf reason=unsupported-animator",
            ctx.frame_input.frame.integral());
        return graph::RenderOpResult(graph::make_render_backend_error(
            graph::RenderBackendErrorCode::UnsupportedCapability,
            "MTSDF text route does not support this animator stack"));
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
    run.color = ::chronon3d::text::pack_text_color(shape.paint.fill);
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
            const auto weight = selector_weight(
                *prepared->reveal_selector, layout.units,
                static_cast<std::uint32_t>(i), sample_time);
            if (weight <= 1e-6f) {
                visible_limit = static_cast<std::uint32_t>(i + 1);
            }
        }
        run.visible_glyph_count = visible_limit;
    }
    sample_gpu_run_level_animation(shape, ctx.frame_input.sample_time, run);
    sample_gpu_text_animation(shape, ctx.frame_input.sample_time, run);
    if (shape.paint.stroke_enabled && shape.paint.stroke_width > 0.0f) {
        run.effect_flags |= 1u;
        run.stroke_width = shape.paint.stroke_width;
        run.stroke_color = ::chronon3d::text::pack_text_color(shape.paint.stroke_color);
    }
    for (const auto& shadow : shape.shadows) {
        if (!shadow.enabled) continue;
        Color shadow_color = shadow.color;
        shadow_color.a = std::clamp(shadow_color.a * shadow.opacity, 0.0f, 1.0f);
        run.shadow_color = ::chronon3d::text::pack_text_color(shadow_color);
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

} // namespace chronon3d::graph::text_run
