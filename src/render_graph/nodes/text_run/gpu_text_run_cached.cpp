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
#include <string>

namespace chronon3d::graph::text_run {

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
                ? "draw_cached_text_run: animated glyph state is not supported by canonical MTSDF"
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
