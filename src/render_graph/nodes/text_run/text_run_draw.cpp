// Cached native MTSDF glyph draw path.
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
#include <chronon3d/runtime/gpu_text_selector_metadata.hpp>
#include <chronon3d/text/mtsdf_preparation.hpp>
#include <chronon3d/text/mtsdf_profile.hpp>
#include <chronon3d/text/glyph_selector.hpp>
#include <chronon3d/text/text_color_packing.hpp>
#include <chronon3d/text/animation/text_animator_properties.hpp>
#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/core/profiling/profiling.hpp>

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
    // A GPU success must never silently drop a per-glyph state the MTSDF
    // shader cannot represent.
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
        if (profiling::g_current_counters) {
            profiling::g_current_counters->text_cache_misses.fetch_add(1, std::memory_order_relaxed);
            profiling::g_current_counters->gpu_text_prepared_batch_build_count.fetch_add(1, std::memory_order_relaxed);
        }
        const auto prep_t0 = profiling::g_current_counters ? profiling::now() : profiling::Clock::time_point{};
        auto created = prepare_mtsdf_text_run(
            *ctx.gpu_glyph_atlas, ctx.asset_resolver, shape);
        if (profiling::g_current_counters) {
            profiling::g_current_counters->text_prepare_per_frame_wall_us.fetch_add(
                static_cast<uint64_t>(std::llround(profiling::elapsed_us(prep_t0))),
                std::memory_order_relaxed);
        }
        if (!created || !created->ready) {
            return graph::RenderOpResult(graph::make_render_backend_error(
                graph::RenderBackendErrorCode::UnsupportedCapability,
                "MTSDF text route has no resident glyphs"));
        }
        ctx.gpu_text_prepared_cache->store(*shape.layout, created);
        prepared = created;
    } else {
        if (profiling::g_current_counters) {
            profiling::g_current_counters->text_cache_hits.fetch_add(1, std::memory_order_relaxed);
            profiling::g_current_counters->gpu_text_prepared_batch_reuse_count.fetch_add(1, std::memory_order_relaxed);
        }
    }

    if (!prepared->ready) {
        return graph::RenderOpResult(graph::make_render_backend_error(
            graph::RenderBackendErrorCode::UnsupportedCapability,
            "MTSDF text route has no resident glyphs"));
    }

    const auto pages_are_bound = [&](
        const runtime::GpuPreparedTextRun& candidate) {
        return std::all_of(candidate.batches.begin(), candidate.batches.end(),
            [&](const runtime::GpuPreparedTextRun::Batch& batch) {
                return batch.page != runtime::kInvalidRenderSurfaceHandle &&
                    ctx.surface_registry->lookup(batch.page).has_value() &&
                    ctx.backend->is_native_surface_valid(batch.page);
            });
    };
    if (!pages_are_bound(*prepared)) {
        // The daemon keeps text caches alive across render jobs, while each
        // job may retire/recreate the Vulkan bindings behind their numeric
        // page handles. Drop both cached layout batches and atlas residency,
        // then rebuild the GPU atlas against the current surface registry.
        spdlog::warn(
            "[text-cache] invalidating prepared MTSDF pages after backend surface retirement");
        ctx.gpu_text_prepared_cache->clear();
        ctx.gpu_glyph_atlas->clear();
        auto rebuilt = prepare_mtsdf_text_run(
            *ctx.gpu_glyph_atlas, ctx.asset_resolver, shape);
        if (!rebuilt || !rebuilt->ready || !pages_are_bound(*rebuilt)) {
            return graph::RenderOpResult(graph::make_render_backend_error(
                graph::RenderBackendErrorCode::UnsupportedCapability,
                "MTSDF text route could not rebuild retired GPU atlas pages"));
        }
        ctx.gpu_text_prepared_cache->store(*shape.layout, rebuilt);
        prepared = std::move(rebuilt);
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
    std::size_t items_drawn = 0;
    const auto draw_t0 = profiling::g_current_counters ? profiling::now() : profiling::Clock::time_point{};
    for (const auto& batch : prepared->batches) {
        if (batch.glyphs.empty()) continue;
        const bool has_resolved_glyph_motion = !shape.animators.empty() ||
            std::any_of(shape.glyphs.begin(), shape.glyphs.end(),
                [](const GlyphInstanceState& glyph) {
                    return glyph.opacity != 1.0f || glyph.position.x != 0.0f ||
                           glyph.position.y != 0.0f || glyph.position.z != 0.0f ||
                           glyph.baseline_shift != 0.0f ||
                           glyph.anchor.x != 0.0f || glyph.anchor.y != 0.0f ||
                           glyph.rotation.x != 0.0f || glyph.rotation.y != 0.0f ||
                           glyph.rotation.z != 0.0f || glyph.skew != 0.0f ||
                           glyph.stroke.a != 0.0f || glyph.background.a != 0.0f ||
                           glyph.scale.x != 1.0f ||
                           glyph.scale.y != 1.0f || glyph.scale.z != 1.0f ||
                           glyph.blur != 0.0f ||
                           glyph.fill.r != 1.0f || glyph.fill.g != 1.0f ||
                           glyph.fill.b != 1.0f || glyph.fill.a != 1.0f;
                });
        const auto dispatch = [&]() -> graph::RenderOpResult {
          if (!has_resolved_glyph_motion) {
            return ctx.backend->draw_text_batch(
                destination.surface_handle(), batch.glyphs,
                std::span<const runtime::TextRunDynamic>(&run, 1),
                std::span<const runtime::RenderSurfaceHandle>(&batch.page, 1));
          }
            // Reuse the immutable atlas batch, but bind each glyph to its own
            // resolved dynamic record. This is the GPU compatibility path for
            // the complete Chronon animator evaluator: arbitrary selectors,
            // expression tracks and stacked animators have already been
            // reduced to the final GlyphInstanceState on the CPU. Geometry,
            // coverage, effects and compositing remain GPU rendered.
            std::vector<runtime::GlyphStatic> glyphs(batch.glyphs.begin(), batch.glyphs.end());
            std::vector<runtime::TextRunDynamic> glyph_runs(
                shape.glyphs.size(), run);
            const bool has_fill_animator = std::any_of(
                shape.animators.begin(), shape.animators.end(),
                [](const TextAnimatorSpec& animator) {
                    return std::any_of(
                        animator.properties.begin(), animator.properties.end(),
                        [](const TextAnimatorProperty& property) {
                            return std::holds_alternative<FillColorProperty>(property);
                        });
                });
            for (auto& glyph : glyphs) {
                const std::size_t state_index = glyph.pad0;
                if (state_index >= shape.glyphs.size()) {
                    return graph::RenderOpResult(graph::make_render_backend_error(
                        graph::RenderBackendErrorCode::InvalidInput,
                        "MTSDF batch glyph index exceeds resolved glyph state"));
                }
                const auto& state = shape.glyphs[state_index];
                glyph.run_index = static_cast<std::uint32_t>(state_index);
                glyph.pad0 = 0; // the record selector addresses one local glyph

                auto& dynamic = glyph_runs[state_index];
                float depth_factor = 1.0f;
                if (std::isfinite(state.position.z) && state.position.z > 0.0f) {
                    depth_factor = 1.0f / (1.0f + state.position.z * 0.001f);
                }
                const float scale_x = state.scale.x * state.scale.z * depth_factor;
                const float scale_y = state.scale.y * state.scale.z * depth_factor;
                dynamic.flags |= 2u;
                dynamic.flags = runtime::pack_text_selector_metadata(
                    dynamic.flags,
                    static_cast<std::uint32_t>(TextSelectorUnit::Glyph),
                    static_cast<std::uint32_t>(TextSelectorOrder::Forward),
                    TextSelectorShape::Square);
                dynamic.selector_start = 0.0f;
                dynamic.selector_end = 100.0f;
                dynamic.selector_amount = 100.0f;
                dynamic.selector_unit_count = 1u;
                dynamic.animator_position_x = state.position.x +
                    state.anchor.x * (1.0f - scale_x);
                dynamic.animator_position_y = state.position.y + state.baseline_shift +
                    state.anchor.y * (1.0f - scale_y);
                dynamic.animator_scale_x = scale_x;
                dynamic.animator_scale_y = scale_y;
                dynamic.animator_rotation_z = state.rotation.z;
                constexpr double kPi = 3.14159265358979323846;
                const double ry_rad = static_cast<double>(state.rotation.y) * (kPi / 180.0);
                const double rx_rad = static_cast<double>(state.rotation.x) * (kPi / 180.0);
                const double ry_clamped = std::clamp(ry_rad, -kPi / 2.0 + 1e-3, kPi / 2.0 - 1e-3);
                const double rx_clamped = std::clamp(rx_rad, -kPi / 2.0 + 1e-3, kPi / 2.0 - 1e-3);
                dynamic.animator_shear_x = static_cast<float>(std::tan(ry_clamped));
                dynamic.animator_shear_y = static_cast<float>(std::tan(rx_clamped));
                dynamic.animator_opacity = state.opacity;
                dynamic.animator_blur = std::max(0.0f, state.blur);
                if (has_fill_animator) {
                    dynamic.flags |= runtime::kTextRunFlagAnimatorFill;
                    dynamic.animator_fill_color =
                        ::chronon3d::text::pack_text_color(state.fill);
                }
            }
            return ctx.backend->draw_text_batch(
                destination.surface_handle(), glyphs, glyph_runs,
                std::span<const runtime::RenderSurfaceHandle>(&batch.page, 1));
        }();
        if (!dispatch.ok()) return dispatch;
        items_drawn += dispatch.value().items_drawn;
    }
    if (profiling::g_current_counters) {
        profiling::g_current_counters->text_draw_wall_us.fetch_add(
            static_cast<uint64_t>(std::llround(profiling::elapsed_us(draw_t0))),
            std::memory_order_relaxed);
    }
    return graph::RenderOpResult(graph::RenderOpOutcome{items_drawn});
}


} // namespace chronon3d::graph::text_run
