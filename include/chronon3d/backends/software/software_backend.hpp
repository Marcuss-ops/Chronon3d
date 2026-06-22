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
// SoftwareRenderer & (draw_node, draw_text_run) remain on SoftwareRenderer
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

// 06 R3b follow-up — SoftwareBackend holds a non-owning back-pointer to
// its orchestrator SoftwareRenderer.  This is a TEMPORARY bridge used by
// `draw_text_run` (which needs the `SoftwareProcessorContext` bundle the
// renderer builds).  When 06 R3 drops dual identity, the back-pointer
// will be replaced with sourcing context directly from `runtime::
class SoftwareRenderer;  // forward decl only — keeps I3 budget

class SoftwareBackend : public graph::RenderBackend {
public:
    /// Construct the backend with the resources it needs from the orchestrator.
    /// All references AND the owner pointer must outlive the backend.
    /// `owner` is required for `draw_text_run` until R3 drops dual identity;
    /// until then passing `nullptr` here will propagate `InvalidInput` from
    /// `renderer::draw_text_run` (defensive loud-fail rather than silent).
    SoftwareBackend(class SoftwareRenderer*            owner,
                    RenderCounters&                    counters,
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

    // 06 R3b — `draw_text_run` and `capabilities` migrated to
    // SoftwareBackend.  SoftwareRenderer no longer exposes these.
    graph::RenderOpResult draw_text_run(Framebuffer& fb,
                                        const TextRunShape& shape,
                                        const Mat4& model_matrix,
                                        float opacity) override;

private:
    RenderCounters&                        m_counters;
    const RenderSettings&                  m_settings;
    std::shared_ptr<cache::FramebufferPool> m_framebuffer_pool;
    // 06 R3b — non-owning back-pointer to the orchestrating SoftwareRenderer,
    // used by `draw_text_run` to source the SoftwareProcessorContext service
    // bundle (font_engine, asset_resolver, debug_config).  Lifetime invariant
    // (verified 06 R3b): m_owner is read ONLY inside `draw_text_run` (a
    // dispatch path, never the destructor), and `~RenderRuntime()` is
    // `= default`, so the field is never dereferenced after
    // `~SoftwareRenderer()`.  When R3 sources context directly from
    // runtime (planned), m_owner will be removed.
    class SoftwareRenderer*                m_owner{nullptr};  // 06 R3b back-pointer
};

} // namespace chronon3d
