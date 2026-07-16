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
#include <chronon3d/backends/software/software_processor_context.hpp>  // TICKET-118: m_proc_ctx value-member
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

// TICKET-118 — the SoftwareRenderer back-pointer has been REMOVED.
// SoftwareBackend is now owner-free at the software layer; the
// orchestrating renderer instead supplies a fully-built
// `SoftwareProcessorContext` via `attach_processor_context()` after
// construction.  This contractive change closes the architectural
// debt that kept the backend tied to its orchestrator (the
// m_owner transient bridge).  See `internal/software_processor_services.hpp`
// for the derivation story.
class SoftwareRenderer;  // forward decl still kept for backward-source compat in callers

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

    /// Attach the fully-resolved `SoftwareProcessorContext` the backend
    /// should use for `draw_node`, `draw_text_run`, and shape-processor
    /// dispatches.  TICKET-118 closed the m_owner back-pointer by
    /// routing the processor-context bundle through this single public
    /// setter instead.  The setter is idempotent; later calls REPLACE
    /// the previous context.  The caller (typically
    /// `runtime_adapter.cpp` or `tests/helpers/test_utils.hpp`) is
    /// responsible for building the context via
    /// `backends::software::internal::make_processor_context` so the
    /// public surface remains source-only.
    ///
    /// Returns void on purpose — this is an unconditional setter, not a
    /// fallible operation.  Construction-time validation (loud-fail on
    /// null REQUIRED services) is still owned by
    /// `make_software_backend()`.
    void attach_processor_context(SoftwareProcessorContext proc_ctx);
    void attach_image_services(ImageRenderer* renderer,
                               image::ImageBackend* backend) noexcept {
        m_proc_ctx.image_renderer = renderer;
        m_proc_ctx.image_backend = backend;
    }

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

    // TICKET-118 — m_owner REMOVED.  Replaced by `m_proc_ctx` which holds
    // the orchestrator-supplied processor-context bundle (set via
    // `attach_processor_context()` immediately after construction).  The
    // `m_proc_ctx.renderer` field is always nullptr after the new
    // construction path — no back-pointer to the SoftwareRenderer exists.
    SoftwareProcessorContext                       m_proc_ctx{};                    // TICKET-118 cached ctx
    ImageRenderer*                                  m_image_renderer{nullptr};
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
