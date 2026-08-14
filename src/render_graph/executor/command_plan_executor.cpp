#include <chronon3d/render_graph/executor/command_plan_executor.hpp>

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
    backend.begin_plan_batch(plan.barriers);
    bool success = true;
    for (const auto& pass : plan.passes.passes) {
        if (!execute_pass(backend, pass)) {
            success = false;
            break;
        }
    }
    backend.end_frame_batch();
    return success;
}

} // namespace chronon3d::runtime
