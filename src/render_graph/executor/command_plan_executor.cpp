#include <chronon3d/render_graph/executor/command_plan_executor.hpp>

#include <spdlog/spdlog.h>

#include <type_traits>
#include <variant>

namespace chronon3d::runtime {
namespace {

using chronon3d::BlendMode;
using chronon3d::Color;
using chronon3d::CompositeOperator;
using chronon3d::graph::RenderBackend;

/// Dispatch one plan pass onto the canonical RenderBackend surface API.
/// Single dispatch site — the executor never branches on the concrete
/// backend type, so a second Vulkan-specific execution path cannot drift in.
bool execute_pass(RenderBackend& backend, const GpuPass& pass) {
    return std::visit(
        [&backend](const auto& params) {
            using P = std::decay_t<decltype(params)>;
            if constexpr (std::is_same_v<P, CompositePass>) {
                return backend.composite_surfaces(
                    params.destination, params.source,
                    params.blend_mode == 1 ? BlendMode::Add : BlendMode::Normal,
                    CompositeOperator::SourceOver).ok();
            } else if constexpr (std::is_same_v<P, TransformPass>) {
                return backend.transform_surface(
                    params.destination, params.source,
                    params.offset_x, params.offset_y, params.opacity).ok();
            } else if constexpr (std::is_same_v<P, AffineTransformPass>) {
                return backend.transform_surface_affine(
                    params.destination, params.source, params.transform).ok();
            } else if constexpr (std::is_same_v<P, BlurPass>) {
                return backend.blur_surface(
                    params.destination, params.source,
                    params.radius, params.horizontal != 0).ok();
            } else if constexpr (std::is_same_v<P, GlowPass>) {
                return backend.glow_surfaces(
                    params.destination, params.source,
                    params.scratch_horizontal, params.scratch_vertical,
                    params.radius, params.intensity,
                    Color{params.tint[0], params.tint[1],
                          params.tint[2], params.tint[3]}).ok();
            } else if constexpr (std::is_same_v<P, ColorAdjustPass>) {
                return backend.color_adjust_surface(
                    params.destination, params.source,
                    params.brightness, params.contrast,
                    Color{params.tint[0], params.tint[1],
                          params.tint[2], params.tint[3]},
                    params.tint_amount).ok();
            } else if constexpr (std::is_same_v<P, MattePass>) {
                return backend.matte_surface(
                    params.destination, params.target, params.matte,
                    params.luma != 0, params.inverted != 0).ok();
            } else if constexpr (std::is_same_v<P, LayerBatchPass>) {
                thread_local std::vector<runtime::LayerInstance> tls_instances;
                tls_instances.clear();
                tls_instances.reserve(params.items.size());
                for (const auto& item : params.items) {
                    runtime::LayerInstance inst;
                    inst.resource_index = 0;
                    for (std::size_t i = 0; i < params.sources.size(); ++i) {
                        if (params.sources[i] == item.source) {
                            inst.resource_index = static_cast<std::uint32_t>(i);
                            break;
                        }
                    }
                    inst.dst_x0 = item.dst_x;
                    inst.dst_y0 = item.dst_y;
                    inst.dst_x1 = item.dst_x + item.dst_w;
                    inst.dst_y1 = item.dst_y + item.dst_h;
                    inst.src_x0 = item.src_u0;
                    inst.src_y0 = item.src_v0;
                    inst.src_x1 = item.src_u1;
                    inst.src_y1 = item.src_v1;
                    inst.opacity = item.opacity;
                    inst.kind = PrimitiveKind::Image;
                    inst.blend = BlendMode::Normal;
                    tls_instances.push_back(inst);
                }
                return backend.execute_layer_batch(
                    params.destination, tls_instances, params.sources, {}, {}).ok();
            }
            return false;  // unreachable: the variant is exhaustive
        },
        pass.params);
}

} // namespace

bool execute_command_plan(graph::RenderBackend& backend,
                          RenderSurfaceRegistry& registry,
                          const CommandPlan& plan) {
    bind_plan_slots(plan.resources, registry);
    backend.begin_plan_batch(plan);
    bool success = true;
    for (std::size_t pass_index = 0; pass_index < plan.passes.passes.size(); ++pass_index) {
        const auto& pass = plan.passes.passes[pass_index];
        // Invalid handles are sentinels, not backend resources.  Passing one
        // through used to make Vulkan fail later in bound_slot(), where the
        // actual offending pass was impossible to identify.  Reject the plan
        // at its single dispatch boundary so callers can take their normal
        // CPU fallback and the diagnostic points at the producer.
        const auto handles = detail::referenced_handles(pass);
        for (const auto handle : handles) {
            if (handle == kInvalidRenderSurfaceHandle) {
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
        if (!execute_pass(backend, pass)) {
            success = false;
            break;
        }
    }
    backend.end_frame_batch();
    return success;
}

} // namespace chronon3d::runtime
