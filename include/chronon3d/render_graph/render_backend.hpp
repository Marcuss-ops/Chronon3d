#pragma once

#include <chronon3d/core/framebuffer.hpp>
#include <chronon3d/scene/camera/camera.hpp>
#include <chronon3d/scene/effects/effect_stack.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <memory>

namespace chronon3d {
    struct RenderNode;
    struct RenderState;
}

namespace chronon3d::graph {

class RenderBackend {
public:
    virtual ~RenderBackend() = default;

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
        const EffectStack& effects
    ) = 0;

    virtual void composite_layer(
        Framebuffer& dest,
        const Framebuffer& src,
        BlendMode mode
    ) = 0;

    virtual void apply_blur(
        Framebuffer& fb,
        float radius
    ) = 0;
};

} // namespace chronon3d::graph
