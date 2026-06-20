#pragma once

// ---------------------------------------------------------------------------
// software_backend.hpp
//
// SoftwareBackend — pure rendering implementation of RenderBackend.
// Extracted from SoftwareRenderer (PR-9) to separate the low-level rendering
// operations (blur, composite, effects, DoF) from the orchestration layer
// (Config, caches, session state, pipeline wiring).
//
// SoftwareRenderer still owns the RenderBackend identity and delegates
// rendering calls to this backend.  Draw operations that require
// SoftwareRenderer& (draw_node, draw_text_run) remain on SoftwareRenderer
// until ShapeProcessor is migrated to accept RenderBackend&.
// ---------------------------------------------------------------------------

#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <memory>

namespace chronon3d {
    struct RenderSettings;
}

namespace chronon3d::renderer {
    class SoftwareRegistry;
}

namespace chronon3d::cache {
    class FramebufferPool;
}

namespace chronon3d {

class SoftwareBackend : public graph::RenderBackend {
public:
    /// Construct the backend with the resources it needs from the orchestrator.
    /// All references must outlive the backend.
    SoftwareBackend(RenderCounters&                    counters,
                    const RenderSettings&              settings,
                    std::shared_ptr<cache::FramebufferPool> pool);

    ~SoftwareBackend() override;

    // Non-copyable, movable.
    SoftwareBackend(const SoftwareBackend&) = delete;
    SoftwareBackend& operator=(const SoftwareBackend&) = delete;
    SoftwareBackend(SoftwareBackend&&) noexcept = default;
    SoftwareBackend& operator=(SoftwareBackend&&) noexcept = default;

    // ── RenderBackend overrides ──────────────────────────────────────────

    [[nodiscard]] graph::RenderCapabilities capabilities() const noexcept override;

    RenderCounters* counters() override { return &m_counters; }
    std::shared_ptr<cache::FramebufferPool> framebuffer_pool() override { return m_framebuffer_pool; }

    void apply_blur(Framebuffer& fb, f32 radius,
                    const std::optional<raster::BBox>& clip = std::nullopt) override;

    void composite_layer(Framebuffer& dst, const Framebuffer& src,
                         BlendMode mode,
                         const std::optional<raster::BBox>& clip = std::nullopt,
                         CompositeOperator op = CompositeOperator::SourceOver) override;

    void apply_effect_stack(Framebuffer& fb, const EffectStack& stack,
                            const effects::EffectExecutionContext& context) override;

    void apply_per_pixel_dof(Framebuffer& framebuffer,
                             std::span<const float> depth,
                             const DepthOfFieldSettings& dof,
                             const LensModel& lens,
                             const std::optional<raster::BBox>& clip) override;

    void draw_node(Framebuffer& fb, const RenderNode& node, const RenderState& state,
                   const Camera& camera, int width, int height) override;

    // draw_text_run remains on SoftwareRenderer because
    // ShapeProcessor::draw() takes SoftwareRenderer& (not RenderBackend&).
    // Once ShapeProcessor is migrated, these can move here.

private:
    RenderCounters&                        m_counters;
    const RenderSettings&                  m_settings;
    std::shared_ptr<cache::FramebufferPool> m_framebuffer_pool;
};

} // namespace chronon3d
