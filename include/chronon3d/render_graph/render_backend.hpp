#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/scene/model/camera/camera.hpp>
#include <chronon3d/scene/model/core/effect_stack.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/compositor/composite_operator.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/effects/effect_execution_context.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <optional>

namespace chronon3d {
    struct RenderNode;
    struct RenderState;
    struct RenderCounters;
    struct TextRunShape;  // forward decl for draw_text_run
}

namespace chronon3d::cache {
    class FramebufferPool;
}

namespace chronon3d::graph {

class RenderBackend {
public:
    RenderBackend() = default;
    virtual ~RenderBackend() = default;
    RenderBackend(const RenderBackend&) = default;
    RenderBackend& operator=(const RenderBackend&) = default;
    RenderBackend(RenderBackend&&) noexcept = default;
    RenderBackend& operator=(RenderBackend&&) noexcept = default;

    virtual RenderCounters* counters() { return nullptr; }
    virtual std::shared_ptr<cache::FramebufferPool> framebuffer_pool() { return nullptr; }

    virtual void draw_node(
        Framebuffer& fb,
        const RenderNode& node,
        const RenderState& state,
        const Camera& camera,
        int width,
        int height
    ) = 0;

    virtual void apply_effect_stack(
        Framebuffer& fb,
        const EffectStack& effects,
        const effects::EffectExecutionContext& context
    ) = 0;

    virtual void composite_layer(
        Framebuffer& dest,
        const Framebuffer& src,
        BlendMode mode,
        const std::optional<raster::BBox>& clip = std::nullopt,
        CompositeOperator op = CompositeOperator::SourceOver
    ) = 0;

    virtual void apply_blur(
        Framebuffer& fb,
        float radius,
        const std::optional<raster::BBox>& clip = std::nullopt
    ) = 0;

    /// Draw a batched text run with per-glyph animation state.
    /// The default implementation returns false (unsupported).
    /// Backends that support text rendering (e.g. SoftwareRenderer)
    /// override this to route through their text-run processor.
    virtual bool draw_text_run(
        Framebuffer& /*fb*/,
        const chronon3d::TextRunShape& /*shape*/,
        const glm::mat4& /*model_matrix*/,
        float /*opacity*/,
        bool /*diagnostic_mode*/
    ) {
        return false;
    }
};

} // namespace chronon3d::graph
