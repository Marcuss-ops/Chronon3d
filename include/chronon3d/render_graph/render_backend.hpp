#pragma once

#include <chronon3d/core/framebuffer.hpp>
#include <chronon3d/scene/camera/camera.hpp>
#include <chronon3d/scene/effects/effect_stack.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <memory>

namespace chronon3d {
    struct RenderNode;
    struct RenderState;
    class RenderTrace;
    struct RenderCounters;
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

    virtual RenderTrace* trace() { return nullptr; }
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
        float time_seconds
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
