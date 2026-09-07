// Public GraphExecutor API wiring. Internal orchestration is compiled in
// executor_internal.cpp instead of being textually included in this TU.
#include <chronon3d/render_graph/executor/graph_executor.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/core/scope/execution_scope.hpp>
#include <chronon3d/internal/runtime/render_session.hpp>
#include <chronon3d/runtime/gpu_device_lost_error.hpp>
#include <chronon3d/runtime/render_surface.hpp>

#include "executor_internal.hpp"

#include <spdlog/spdlog.h>

#include <type_traits>
#include <string>
#include <utility>
#include <variant>

namespace chronon3d::graph {
namespace {

[[nodiscard]] runtime::FrameExecutionResult frame_result_from_session(
    const RenderSession& session) {
    const auto error = session.last_frame_error();
    if (runtime::gpu_worker_poisoned()) {
        std::string reason = "GPU worker is poisoned after a terminal device-loss failure";
        if (error && !error->message.empty()) reason = error->message;
        return runtime::FrameExecutionResult::failed(
            runtime::ExecutionFailure::DeviceLost, std::move(reason));
    }
    if (!error) {
        return runtime::FrameExecutionResult::failed(
            runtime::ExecutionFailure::InvalidResource,
            "graph execution returned no framebuffer without a diagnostic");
    }
    runtime::ExecutionFailure failure = runtime::ExecutionFailure::InvalidResource;
    switch (error->backend_code) {
      case RenderBackendErrorCode::UnsupportedCapability:
      case RenderBackendErrorCode::InvalidInput:
        // Missing capability or malformed input: the request is at fault,
        // not the device.
        failure = runtime::ExecutionFailure::InvalidResource; break;
      case RenderBackendErrorCode::ExecutionFailure:
        // Real runtime/backend fault (shaping, raster, surface/device call).
        // Kept distinct so telemetry and retry policy never mistake an
        // execution failure for invalid input.
        failure = runtime::ExecutionFailure::BackendFailure; break;
    }
    std::string reason;
    if (!error->node_name.empty()) reason = error->node_name + ": ";
    reason += error->message;
    return runtime::FrameExecutionResult::failed(failure, std::move(reason));
}

} // namespace

FrameExecutionOutput GraphExecutor::execute(
    CompiledFrameGraph& compiled,
    RenderGraphContext& ctx,
    ExecutionScope& scope,
    ExecutionScheduler& scheduler
) const {
    auto& session = scope.session();
    try {
        auto framebuffer = execute_internal(
            compiled, ctx, session, scope.arena(), scheduler);
        if (framebuffer) {
            return FrameExecutionOutput{
                .framebuffer = std::move(framebuffer),
                .result = runtime::FrameExecutionResult::succeeded(),
            };
        }
        return FrameExecutionOutput{
            .framebuffer = nullptr,
            .result = frame_result_from_session(session),
        };
    } catch (const runtime::GpuDeviceLostError& error) {
        return FrameExecutionOutput{
            .framebuffer = nullptr,
            .result = runtime::FrameExecutionResult::failed(
                runtime::ExecutionFailure::DeviceLost, error.what()),
        };
    }
}

bool GraphExecutor::execute(
    RenderBackend& backend,
    runtime::RenderSurfaceRegistry& registry,
    const runtime::CommandPlan& plan) const {
    using chronon3d::BlendMode;
    using chronon3d::Color;
    using chronon3d::CompositeOperator;

    runtime::bind_plan_slots(plan.resources, registry);
    backend.begin_plan_batch(plan);

    bool success = true;
    for (std::size_t pass_index = 0;
         pass_index < plan.passes.passes.size();
         ++pass_index) {
        const auto& pass = plan.passes.passes[pass_index];
        for (const auto handle : runtime::detail::referenced_handles(pass)) {
            if (handle == runtime::kInvalidRenderSurfaceHandle) {
                spdlog::error(
                    "[render-graph] refusing pass {} with invalid surface handle 0 "
                    "(kind={}, resources={}, allocations={})",
                    pass_index, static_cast<int>(pass.kind),
                    plan.resources.slots.size(), plan.resources.allocations.size());
                success = false;
                break;
            }
        }
        if (!success) break;

        if (pass.dynamic_parameters.size != 0) {
            if (!plan.dynamic_parameters) {
                spdlog::error(
                    "[render-graph] pass {} references dynamic parameters without a table",
                    pass_index);
                success = false;
                break;
            }
            try {
                backend.bind_compiled_parameters(plan.dynamic_parameters->bytes(
                    pass.dynamic_parameters.offset, pass.dynamic_parameters.size));
            } catch (const std::out_of_range&) {
                spdlog::error(
                    "[render-graph] pass {} has an out-of-range dynamic parameter span",
                    pass_index);
                success = false;
                break;
            }
        }

        const bool pass_ok = std::visit(
            [&backend](const auto& params) {
                using P = std::decay_t<decltype(params)>;
                if constexpr (std::is_same_v<P, runtime::CompositePass>) {
                    return backend.composite_surfaces(
                        params.destination, params.source,
                        params.blend_mode == 1 ? BlendMode::Add : BlendMode::Normal,
                        CompositeOperator::SourceOver).ok();
                } else if constexpr (std::is_same_v<P, runtime::TransformPass>) {
                    return backend.transform_surface(
                        params.destination, params.source, params.offset_x,
                        params.offset_y, params.opacity).ok();
                } else if constexpr (std::is_same_v<P, runtime::AffineTransformPass>) {
                    return backend.transform_surface_affine(
                        params.destination, params.source, params.transform).ok();
                } else if constexpr (std::is_same_v<P, runtime::BlurPass>) {
                    return backend.blur_surface(
                        params.destination, params.source, params.radius,
                        params.horizontal != 0).ok();
                } else if constexpr (std::is_same_v<P, runtime::GlowPass>) {
                    return backend.glow_surfaces(
                        params.destination, params.source, params.scratch_horizontal,
                        params.scratch_vertical, params.radius, params.intensity,
                        Color{params.tint[0], params.tint[1], params.tint[2],
                              params.tint[3]}).ok();
                } else if constexpr (std::is_same_v<P, runtime::YuvOverlayPass>) {
                    return backend.yuv_overlay_surface(
                        params.destination, params.source, params.format,
                        params.dirty_region, params.mode).ok();
                } else if constexpr (std::is_same_v<P, runtime::ColorAdjustPass>) {
                    return backend.color_adjust_surface(
                        params.destination, params.source, params.brightness,
                        params.contrast,
                        Color{params.tint[0], params.tint[1], params.tint[2],
                              params.tint[3]}, params.tint_amount).ok();
                } else if constexpr (std::is_same_v<P, runtime::MattePass>) {
                    return backend.matte_surface(
                        params.destination, params.target, params.matte,
                        params.luma != 0, params.inverted != 0).ok();
                } else if constexpr (std::is_same_v<P, runtime::LayerBatchPass>) {
                    thread_local std::vector<runtime::LayerInstance> instances;
                    instances.clear();
                    instances.reserve(params.items.size());
                    for (const auto& item : params.items) {
                        runtime::LayerInstance instance;
                        instance.resource_index = 0;
                        for (std::size_t i = 0; i < params.sources.size(); ++i) {
                            if (params.sources[i] == item.source) {
                                instance.resource_index = static_cast<std::uint32_t>(i);
                                break;
                            }
                        }
                        instance.dst_x0 = item.dst_x;
                        instance.dst_y0 = item.dst_y;
                        instance.dst_x1 = item.dst_x + item.dst_w;
                        instance.dst_y1 = item.dst_y + item.dst_h;
                        instance.src_x0 = item.src_u0;
                        instance.src_y0 = item.src_v0;
                        instance.src_x1 = item.src_u1;
                        instance.src_y1 = item.src_v1;
                        instance.opacity = item.opacity;
                        instance.kind = runtime::PrimitiveKind::Image;
                        instance.blend = BlendMode::Normal;
                        instances.push_back(instance);
                    }
                    return backend.execute_layer_batch(
                        params.destination, instances, params.sources, {}, {}).ok();
                } else if constexpr (std::is_same_v<P, runtime::ScalePass>) {
                    runtime::SurfaceAffineTransform transform{};
                    transform.source_x[0] = params.scale_x != 0.0f
                        ? 1.0f / params.scale_x : 1.0f;
                    transform.source_y[1] = params.scale_y != 0.0f
                        ? 1.0f / params.scale_y : 1.0f;
                    transform.opacity = 1.0f;
                    transform.bilinear = static_cast<std::uint32_t>(
                        params.filter_mode != 0);
                    return backend.transform_surface_affine(
                        params.destination, params.source, transform).ok();
                } else if constexpr (std::is_same_v<P, runtime::TextBatchPass>) {
                    backend.draw_text_batch(
                        params.destination, params.glyphs, params.runs,
                        params.atlas_pages);
                    return true;
                }
                return false;
            },
            pass.params);

        if (!pass_ok) {
            success = false;
            break;
        }
    }

    backend.end_frame_batch();
    return success;
}

} // namespace chronon3d::graph
