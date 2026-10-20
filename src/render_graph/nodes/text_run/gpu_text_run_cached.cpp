// SPDX-License-Identifier: MIT
//
// Canonical cached GPU text bridge. The prepared MTSDF route is the only
// native route; the former CPU-rasterized packed-atlas compatibility route was
// deliberately removed once the MTSDF corpus and hardware canary were green.

#include "gpu_text_run.hpp"
#include "gpu_text_run_detail.hpp"
#include "gpu_text_cached_policy.hpp"

#include <chronon3d/core/profiling/profiling.hpp>
#include <spdlog/spdlog.h>

#include <cstdlib>
#include <cmath>
#include <sstream>
#include <string>

namespace chronon3d::graph::text_run {

namespace {

std::string unsupported_animation_detail(const TextRunShape& shape) {
    std::ostringstream detail;
    detail << " animators=" << shape.animators.size()
           << " animated_doc=" << static_cast<bool>(shape.animated_doc)
           << " fill_style=" << static_cast<bool>(shape.paint.fill_style)
           << " stroke_style=" << static_cast<bool>(shape.paint.stroke_style)
           << " stroke_enabled=" << shape.paint.stroke_enabled
           << " stroke_width=" << shape.paint.stroke_width
           << " stroke_cap=" << static_cast<int>(shape.paint.stroke_cap)
           << " stroke_join=" << static_cast<int>(shape.paint.stroke_join)
           << " miter_limit=" << shape.paint.stroke_miter_limit
           << " dash_count=" << shape.paint.stroke_dash_array.size()
           << " dash_offset=" << shape.paint.stroke_dash_offset
           << " material=" << shape.material.enabled
           << " shadows=" << shape.shadows.size()
           << " glyphs=" << shape.glyphs.size()
           << " placed=" << (shape.layout ? shape.layout->placed.glyphs.size() : 0);
    for (std::size_t i = 0; i < shape.shadows.size(); ++i) {
        const auto& shadow = shape.shadows[i];
        detail << " shadow" << i << "_enabled=" << shadow.enabled
               << "_blur=" << shadow.blur
               << "_offset=" << shadow.offset.x << ',' << shadow.offset.y;
    }
    for (std::size_t i = 0; i < shape.glyphs.size(); ++i) {
        const auto& glyph = shape.glyphs[i];
        if (std::abs(glyph.rotation.x) > 1e-6f ||
            std::abs(glyph.rotation.y) > 1e-6f ||
            std::abs(glyph.rotation.z) > 1e-6f ||
            std::abs(glyph.position.z) > 1e-6f ||
            std::abs(glyph.anchor.z) > 1e-6f ||
            std::abs(glyph.skew) > 1e-6f ||
            std::abs(glyph.skew_axis) > 1e-6f ||
            glyph.background.a > 1e-6f || glyph.stroke.a > 1e-6f ||
            glyph.stroke_width > 1e-6f || glyph.background_padding_em > 1e-6f ||
            glyph.background_radius_em > 1e-6f) {
            detail << " first_unsupported_glyph=" << i
                   << " rotation=" << glyph.rotation.x << ',' << glyph.rotation.y << ',' << glyph.rotation.z
                   << " position_z=" << glyph.position.z
                   << " anchor_z=" << glyph.anchor.z
                   << " skew=" << glyph.skew
                   << " skew_axis=" << glyph.skew_axis
                   << " background_alpha=" << glyph.background.a
                   << " stroke_alpha=" << glyph.stroke.a
                   << " stroke_width=" << glyph.stroke_width
                   << " background_padding=" << glyph.background_padding_em
                   << " background_radius=" << glyph.background_radius_em;
            break;
        }
    }
    return detail.str();
}

} // namespace

graph::RenderOpResult draw_cached_text_run(
    RenderGraphContext& ctx,
    Framebuffer& destination,
    const TextRunShape& shape,
    const glm::mat4& model_matrix,
    float opacity) {
    const bool force_software = std::getenv("CHRONON3D_FORCE_SOFTWARE_TEXT") != nullptr;
    const bool lowerable_gpu_animation =
        !shape.animators.empty() && can_lower_gpu_text_animation(shape);
    const bool unsupported_animation =
        !shape.animators.empty() && !lowerable_gpu_animation;
    const bool native_surfaces_supported =
        ctx.backend != nullptr && ctx.backend->supports_native_surfaces();
    const auto route_decision = detail::resolve_text_execution_route(
        unsupported_animation,
        native_surfaces_supported,
        force_software,
        ctx.gpu_text_prepared_cache != nullptr,
        ctx.backend != nullptr,
        ctx.surface_registry != nullptr);

    if (profiling::g_current_counters) {
        switch (route_decision.route) {
            case detail::TextExecutionRoute::NativeMtsdf:
                profiling::g_current_counters->gpu_text_route_native_mtsdf_count
                    .fetch_add(1, std::memory_order_relaxed);
                break;
            case detail::TextExecutionRoute::Software:
                profiling::g_current_counters->gpu_text_route_software_count
                    .fetch_add(1, std::memory_order_relaxed);
                break;
            case detail::TextExecutionRoute::Reject:
                profiling::g_current_counters->gpu_text_route_reject_count
                    .fetch_add(1, std::memory_order_relaxed);
                break;
        }
    }

    if (ctx.policy.diagnostics_enabled) {
        const auto frame_number = ctx.frame_input.frame.integral();
        spdlog::info(
            "[text-route] frame={} route={} reason={} unsupported_animation={} "
            "forced_software={} native_surfaces={} prepared_cache={} backend={} "
            "surface_registry={}",
            frame_number,
            detail::text_execution_route_name(route_decision.route),
            detail::text_route_reason_name(route_decision.reason),
            unsupported_animation, force_software, native_surfaces_supported,
            ctx.gpu_text_prepared_cache != nullptr, ctx.backend != nullptr,
            ctx.surface_registry != nullptr);
    }

    // Route counters above remain the normal per-job observability; this
    // detailed frame-level record is opt-in so production rendering does not
    // format or emit one INFO message per text run per frame.

    if (route_decision.route == detail::TextExecutionRoute::Reject) {
        return graph::RenderOpResult(graph::make_render_backend_error(
            graph::RenderBackendErrorCode::UnsupportedCapability,
            route_decision.reason == detail::TextRouteReason::UnsupportedAnimation
                ? "draw_cached_text_run: animated glyph state is not supported by canonical MTSDF" +
                    unsupported_animation_detail(shape)
                : "draw_cached_text_run: canonical MTSDF resources are unavailable"));
    }
    if (route_decision.route == detail::TextExecutionRoute::Software) {
        return graph::RenderOpResult(graph::make_render_backend_error(
            graph::RenderBackendErrorCode::UnsupportedCapability,
            std::string("draw_cached_text_run: software text fallback (") +
                detail::text_route_reason_name(route_decision.reason) + ")"));
    }

    // There is intentionally no second native route here. A lowerable MTSDF
    // dispatch failure is returned to text_run_execution, which owns the
    // explicit software fallback or the REQUIRE_GPU_NATIVE failure contract.
    return draw_cached_text_run_mtsdf(
        ctx, destination, shape, model_matrix, opacity);
}
} // namespace chronon3d::graph::text_run
