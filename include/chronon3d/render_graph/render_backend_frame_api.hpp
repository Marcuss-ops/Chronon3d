#pragma once

#include <chronon3d/render_graph/render_backend_types.hpp>

#include <chronon3d/effects/effect_execution_context.hpp>
#include <chronon3d/scene/model/camera/camera.hpp>
#include <chronon3d/scene/model/camera/dof.hpp>
#include <chronon3d/scene/model/core/effect_stack.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/compositor/composite_operator.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/runtime/gpu_command_plan.hpp>

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace chronon3d {
struct RenderNode;
struct RenderState;
}

namespace chronon3d::graph {

/// Frame and command recording lifecycle plus CPU-facing node operations.
class RenderBackendFrameApi {
public:
    virtual ~RenderBackendFrameApi() = default;

    virtual void begin_frame_batch();
    virtual void end_frame_batch();
    /// Publish native GPU writes between Render Graph levels without
    /// submitting the frame's command buffer.
    virtual void synchronize_native_dependencies();
    virtual void begin_plan_batch(const runtime::CommandPlan& plan);
    virtual void bind_compiled_parameters(std::span<const std::byte> parameters);
    virtual void begin_command_batch();
    virtual void end_command_batch();
    virtual void release_frame_transient_surfaces() noexcept;
    virtual void retire_frame_transient_surfaces() noexcept;

    virtual void apply_per_pixel_dof(
        Framebuffer& framebuffer, std::span<const float> depth,
        const DepthOfFieldSettings& dof, const LensModel& lens,
        const std::optional<raster::BBox>& clip) = 0;
    virtual RenderOpResult draw_node(
        Framebuffer& framebuffer, const RenderNode& node, const RenderState& state,
        const Camera& camera, int width, int height);
    [[nodiscard]] virtual std::optional<RenderBackendError> validate_render_node(
        const RenderNode& node) const;
    virtual void apply_effect_stack(
        Framebuffer& framebuffer, const EffectStack& effects,
        const effects::EffectExecutionContext& context) = 0;
    virtual void composite_layer(
        Framebuffer& destination, const Framebuffer& source, BlendMode mode,
        const std::optional<raster::BBox>& clip = std::nullopt,
        CompositeOperator op = CompositeOperator::SourceOver) = 0;
    virtual void apply_blur(
        Framebuffer& framebuffer, float radius,
        const std::optional<raster::BBox>& clip = std::nullopt) = 0;
};

} // namespace chronon3d::graph
