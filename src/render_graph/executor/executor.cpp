// Public GraphExecutor API wiring. Internal orchestration is compiled in
// executor_internal.cpp instead of being textually included in this TU.
#include <chronon3d/render_graph/executor/graph_executor.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/core/scope/execution_scope.hpp>
#include <chronon3d/runtime/job/job_session.hpp>
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
    const runtime::JobSession& session) {
    const auto error = session.last_frame_error();
    if (runtime::gpu_worker_poisoned()) {
        std::string reason = "GPU worker is poisoned after a terminal device-loss failure";
        if (error && !error->message.empty()) reason = error->message;
        return runtime::FrameExecutionResult::failed(
            chronon::make_error(chronon::ErrorKind::DeviceLost,
                                "gpu", "DeviceLost", std::move(reason),
                                "graph_executor"));
    }
    if (!error) {
        return runtime::FrameExecutionResult::failed(
            chronon::make_error(
                chronon::ErrorKind::InvariantViolation, "graph_executor",
                "MissingFrameError", "graph execution returned no framebuffer without a diagnostic"));
    }
    return runtime::FrameExecutionResult::failed(error->canonical_error());
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
        auto result = frame_result_from_session(session);
        session.fail_job(result.error);
        return FrameExecutionOutput{
            .framebuffer = nullptr,
            .result = std::move(result),
        };
    } catch (const runtime::GpuDeviceLostError& error) {
        auto result = runtime::FrameExecutionResult::failed(
            chronon::make_error(chronon::ErrorKind::DeviceLost,
                                "gpu", "DeviceLost", error.what(),
                                "graph_executor"));
        session.fail_job(result.error);
        return FrameExecutionOutput{
            .framebuffer = nullptr,
            .result = std::move(result),
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

        const auto pass_result = std::visit(
            [&backend](const auto& params) -> RenderOpResult {
                using P = std::decay_t<decltype(params)>;
                if constexpr (std::is_same_v<P, runtime::CompositePass>) {
                    return backend.composite_surfaces(
                        params.destination, params.source,
                        params.blend_mode == 1 ? BlendMode::Add : BlendMode::Normal,
                        CompositeOperator::SourceOver);
                } else if constexpr (std::is_same_v<P, runtime::TransformPass>) {
                    return backend.transform_surface(
                        params.destination, params.source, params.offset_x,
                        params.offset_y, params.opacity);
                } else if constexpr (std::is_same_v<P, runtime::AffineTransformPass>) {
                    return backend.transform_surface_affine(
                        params.destination, params.source, params.transform);
                } else if constexpr (std::is_same_v<P, runtime::BlurPass>) {
                    return backend.blur_surface(
                        params.destination, params.source, params.radius,
                        params.horizontal != 0);
                } else if constexpr (std::is_same_v<P, runtime::GlowPass>) {
                    return backend.glow_surfaces(
                        params.destination, params.source, params.scratch_horizontal,
                        params.scratch_vertical, params.radius, params.intensity,
                        Color{params.tint[0], params.tint[1], params.tint[2],
                              params.tint[3]});
                } else if constexpr (std::is_same_v<P, runtime::YuvOverlayPass>) {
                    return backend.yuv_overlay_surface(
                        params.destination, params.source, params.format,
                        params.dirty_region, params.mode);
                } else if constexpr (std::is_same_v<P, runtime::ColorAdjustPass>) {
                    return backend.color_adjust_surface(
                        params.destination, params.source, params.brightness,
                        params.contrast,
                        Color{params.tint[0], params.tint[1], params.tint[2],
                              params.tint[3]}, params.tint_amount);
                } else if constexpr (std::is_same_v<P, runtime::MattePass>) {
                    return backend.matte_surface(
                        params.destination, params.target, params.matte,
                        params.luma != 0, params.inverted != 0);
                } else if constexpr (std::is_same_v<P, runtime::LayerBatchPass>) {
                    thread_local std::vector<runtime::LayerInstance> instances;
                    instances.clear();
                    instances.reserve(params.items.size());
                    for (const auto& item : params.items) {
                        if (item.resource_index == runtime::kInvalidLayerResourceIndex ||
                            item.resource_index >= params.sources.size() ||
                            params.sources[item.resource_index] != item.source) {
                            return RenderOpResult(make_render_backend_error(
                                RenderBackendErrorCode::InvalidInput,
                                "layer batch item has no valid compiled source/resource binding"));
                        }
                        runtime::LayerInstance instance;
                        instance.resource_index = item.resource_index;
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
                        params.destination, instances, params.sources, {}, {});
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
                        params.destination, params.source, transform);
                } else if constexpr (std::is_same_v<P, runtime::TextBatchPass>) {
                    return backend.draw_text_batch(
                        params.destination, params.glyphs, params.runs,
                        params.atlas_pages);
                }
                return RenderOpResult(make_render_backend_error(
                    RenderBackendErrorCode::UnsupportedCapability,
                    "unsupported command plan pass variant"));
            },
            pass.params);

        if (!pass_result.ok()) {
            spdlog::error(
                "[render-graph] pass {} (kind={}) failed: [{}] {}",
                pass_index, static_cast<int>(pass.kind),
                render_backend_error_code_name(pass_result.error().code),
                pass_result.error().message);
            success = false;
            break;
        }
    }

    backend.end_frame_batch();
    return success;
}

} // namespace chronon3d::graph
