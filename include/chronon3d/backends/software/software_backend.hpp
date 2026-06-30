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
#include <chronon3d/backends/software/software_backend_services.hpp>
#include <memory>

namespace chronon3d {
    struct RenderSettings;
    struct TextRenderResources;
    namespace assets { class AssetResolver; }
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
// will be replaced with sourcing context directly from `runtime::`
class SoftwareRenderer;  // forward decl only — keeps I3 budget

class SoftwareBackend : public graph::RenderBackend {
public:
    /// Construction path — `services` is the typed bundle produced by
    /// `make_software_backend(...)`.  Direct construction (without the
    /// factory) is permitted but skips validation; production code MUST
    /// use the factory so null services fail loudly at construction time
    /// rather than at first draw_text_run dispatch.
    ///
    /// Lifetime: every non-null pointer in `services` must outlive the
    /// backend; `services.framebuffer_pool` (shared_ptr) is mirror-stored
    /// on the backend so `RenderBackend::framebuffer_pool()` can return
    /// by-value.
    explicit SoftwareBackend(SoftwareBackendServices services);

    ~SoftwareBackend() override;

    // Non-copyable, movable.
    SoftwareBackend(const SoftwareBackend&) = delete;
    SoftwareBackend& operator=(const SoftwareBackend&) = delete;
    SoftwareBackend(SoftwareBackend&&) noexcept = default;
    SoftwareBackend& operator=(SoftwareBackend&&) noexcept = default;

    // ── RenderBackend overrides ──────────────────────────────────────────

    [[nodiscard]] graph::RenderCapabilities capabilities() const noexcept override;

    RenderCounters* counters() override { return m_counters; }
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
    // ── Non-owning required-service pointers ──
    // All fields are non-owning references to services whose lifetime is
    // managed by the caller (RenderRuntime / renderer) per the
    // SoftwareBackendServices docs.  FramebufferPool is the sole owning
    // reference (shared_ptr) so the same pool can be shared by multiple
    // backends while RenderBackend::framebuffer_pool() can return by-value.
    RenderCounters*                                m_counters{nullptr};            // REQUIRED
    const RenderSettings*                          m_settings{nullptr};            // REQUIRED
    std::shared_ptr<cache::FramebufferPool>        m_framebuffer_pool;             // REQUIRED (shared)
    const assets::AssetResolver*                   m_asset_resolver{nullptr};      // REQUIRED (draw_text_run)
    TextRenderResources*                           m_text_resources{nullptr};      // REQUIRED (draw_text_run)

    // 06 R3b — non-owning back-pointer to the orchestrating SoftwareRenderer,
    // used by `draw_text_run` to source the SoftwareProcessorContext service
    // bundle (font_engine, asset_resolver, debug_config).  Lifetime invariant
    // (verified 06 R3b): m_owner is read ONLY inside `draw_text_run` (a
    // dispatch path, never the destructor), and `~RenderRuntime()` is
    // `= default`, so the field is never dereferenced after
    // `~SoftwareRenderer()`.  When R3 sources context directly from
    // runtime (planned), m_owner will be removed.
    class SoftwareRenderer*                        m_owner{nullptr};  // 06 R3b back-pointer
};

// ═════════════════════════════════════════════════════════════════════════════
//  make_software_backend — validation-gate factory.
//
//  On any null REQUIRED service, returns
//  `Result::err(SoftwareBackendServicesError{...})` carrying the field
//  name + Code. Debug builds additionally `assert(false)` so the
//  failure surfaces in unit tests even when the caller forgets to
//  inspect the Result.
//
//  Production callers SHOULD use `.value()` immediately so a malformed
//  services bundle fails at construction (not at first draw call).
//
//  Defined in `src/backends/software/software_backend.cpp` to avoid a
//  new TU + a CMake edit (matches the `make_processor_context`
//  precedent).
// ═════════════════════════════════════════════════════════════════════════════
[[nodiscard]] Result<std::unique_ptr<SoftwareBackend>, SoftwareBackendServicesError>
make_software_backend(SoftwareBackendServices services);

} // namespace chronon3d
