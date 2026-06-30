#pragma once

// ============================================================================
// backends/software/software_renderer.hpp — 06 R3b single-identity orchestrator.
// Inherits from Renderer ONLY.  All graph::RenderBackend polymorphism lives on
// the attached SoftwareBackend, reached through m_runtime->backend().
// capabilities / draw_text_run live exclusively on SoftwareBackend.
// ============================================================================

#include <chronon3d/backends/software/renderer.hpp>
#include <chronon3d/backends/software/render_settings.hpp>
#include <chronon3d/backends/assets/image_renderer.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/backends/software/software_render_session.hpp>
#include <chronon3d/backends/text/text_render_resources.hpp>   // FontPreflightSummary
#include <chronon3d/core/config.hpp>

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>

namespace chronon3d {

struct TextRenderResources;

class FontEngine; class Composition; class Scene; class Camera; class Camera2_5D;
class CompositionRegistry;
namespace media { class MediaFrameProvider; }
namespace image { class ImageBackend; }
namespace runtime { class RenderRuntime; }
namespace raster { struct BBox; }
namespace cache { class FramebufferPool; class NodeCache; }
namespace graph {
  class RenderBackend; class CompiledGraphCache;
  class GraphNodeCatalog; class SceneHasher; class SceneProgramStore;
  // Note: executor access was removed; route via `runtime().executor()`.
}
namespace effects { class EffectCatalog; struct EffectExecutionContext; }
namespace renderer { class SoftwareRegistry; }

struct MotionBlurSettings; struct DepthOfFieldSettings; struct LensModel;
struct Frame; struct LayerBBoxState; struct RenderNode; struct RenderState;
struct EffectStack;
// Forward-declared as the same enum class as the canonical declarations
// in `<chronon3d/compositor/*>`.  Underlying type matches so cross-TU
// identities stay aligned (`unsigned char` here == `uint8_t` there).
enum class BlendMode : unsigned char;
enum class CompositeOperator : unsigned char;

class SoftwareRenderer : public Renderer {
public:
    // ── Render entry points ────────────────────────────────────────────
    // P1-F Pass B — `render_frame` was canonicalised to `render` to align
    // with the V0.2 SDK API (`chronon3d::sdk::RenderEngine::render`).  Return
    // type + semantics preserved exactly; pure renaming.  This header is now
    // in `src/backends/software/include_private/` since P1-E Pass E, so the
    // bulk `.render_frame(` sed in 70 caller files did NOT see this
    // declaration line — it has to be renamed here alongside the .cpp body.
    std::shared_ptr<Framebuffer> render(const Composition& comp, Frame frame);
    std::shared_ptr<Framebuffer> render_scene(const Scene& scene, const Camera& camera,
                                              i32 width, i32 height);
    std::shared_ptr<Framebuffer> render_scene(const Scene& scene,
                                              const std::optional<Camera2_5D>& camera,
                                              i32 width, i32 height) override;
    [[nodiscard]] std::string debug_render_graph(const Scene& scene, const Camera& camera,
                                                 i32 width, i32 height, Frame frame = 0,
                                                 f32 frame_time = 0.0f) const;

    // Cat-2 font preflight (Cat-2 framing per AGENTS.md freeze audit).
    // Public preflight entry: walks scene.layers() once, collects a
    // representative (font_path, font_size) per text layer, and calls
    // text_render_resources()->resolve_handle(...) for each unique pair
    // so the BLFontFace + FreeType caches are primed BEFORE render starts.
    // Returned FontPreflightSummary drives diagnostics; render proceeds
    // even when preflight_missing>0 (missing fonts fall through to BL/FT
    // load-failure paths, same behaviour as the pre-preflight era).
    //
    // Auto-wired inside render_scene(): the auto path arms
    // set_debug_io_fence(true) AFTER preflighting and disarms on render
    // return so any unforeseen font (added after preflight) throws loudly
    // instead of silently doing synchronous I/O on the render thread.
    [[nodiscard]] chronon3d::FontPreflightSummary preflight_fonts(
        const Scene& scene,
        const assets::AssetResolver& resolver
    );

    // ── Construction / destruction ─────────────────────────────────────
    explicit SoftwareRenderer(runtime::RenderRuntime& rt, Config config);
    explicit SoftwareRenderer(Config config);
    ~SoftwareRenderer() override;
    // Move ops use real rvalue-ref (no EAST-CONST hack). See .cpp.
    SoftwareRenderer(SoftwareRenderer const&) = delete;
    SoftwareRenderer const& operator=(SoftwareRenderer const&) = delete;
    SoftwareRenderer(SoftwareRenderer&& other) noexcept;
    SoftwareRenderer& operator=(SoftwareRenderer&& other) noexcept;

    // ── Settings / diagnostics (multi-line bodies live in .cpp) ────────
    void set_settings(const RenderSettings& settings);
    void set_motion_blur(MotionBlurSettings mb);
    void set_diagnostic_mode(bool enabled);
    [[nodiscard]] bool is_diagnostic_mode() const { return m_settings.diagnostics.enabled; }
    void reset_counters();
    // `render_settings()` is the canonical accessor; the legacy
    // `settings()` alias was removed (item 2 mega-facade cleanup).
    [[nodiscard]] const RenderSettings& render_settings() const { return m_settings; }
    [[nodiscard]] const MotionBlurSettings& motion_blur() const { return m_settings.motion_blur; }

    // ── Cache operations ───────────────────────────────────────────────
    void clear_caches();
    void clear_node_cache()   { node_cache().clear(); }
    void set_composition_registry(const CompositionRegistry* r) { m_registry = r; }
    [[nodiscard]] const CompositionRegistry* composition_registry() const { return m_registry; }

    // ── Rendering facade ───────────────────────────────────────────────
    void apply_per_pixel_dof(Framebuffer& fb, std::span<const float> depth,
        const DepthOfFieldSettings& dof, const LensModel& lens,
        const std::optional<raster::BBox>& clip);
    void draw_node(Framebuffer& fb, const RenderNode& node, const RenderState& state,
                   const Camera& camera, i32 width, i32 height);
    void apply_blur(Framebuffer& fb, f32 radius,
                    const std::optional<raster::BBox>& clip = std::nullopt);
    void apply_effect_stack(Framebuffer& fb, const EffectStack& stack,
                            const effects::EffectExecutionContext& context);
    void composite_layer(Framebuffer& dst, const Framebuffer& src, BlendMode mode,
                         const std::optional<raster::BBox>& clip = std::nullopt,
                         CompositeOperator op = CompositeOperator::SourceOver);

    // IMPORTANT: `capabilities()` and `draw_text_run()` are NOT exposed
    // here.  They live exclusively on `SoftwareBackend`.  Callers that
    // need them reach them through `sw_renderer.backend().capabilities()`
    // or `sw_renderer.backend().draw_text_run(...)`.

    // ── Image + video ──────────────────────────────────────────────────
    void set_image_backend(std::shared_ptr<image::ImageBackend> backend);
    void set_video_decoder(std::shared_ptr<media::MediaFrameProvider> d) { m_video_decoder = std::move(d); }
    [[nodiscard]] media::MediaFrameProvider* video_decoder() const { return m_video_decoder.get(); }
    [[nodiscard]] image::ImageBackend* image_backend() const { return m_image_backend.get(); }
    [[nodiscard]] ImageRenderer& image_renderer() { return m_image_renderer; }

    // ── Dirty-rect telemetry (inline reads) ────────────────────────────
    [[nodiscard]] double last_dirty_area_ratio() const    { return m_session.common.dirty_telemetry.last_dirty_area_ratio; }
    [[nodiscard]] bool    last_dirty_rect_enabled() const { return m_session.common.dirty_telemetry.last_dirty_rect_enabled; }
    [[nodiscard]] std::optional<raster::BBox> last_dirty_rect() const { return m_session.common.dirty_telemetry.last_dirty_rect; }
    [[nodiscard]] bool    last_tile_execution_used() const { return m_session.common.dirty_telemetry.last_tile_execution_used; }
    [[nodiscard]] bool    last_fast_path_reused() const  { return m_session.common.dirty_telemetry.last_fast_path_reused; }
    [[nodiscard]] bool    last_graph_reused() const      { return m_session.common.dirty_telemetry.last_graph_reused; }
    [[nodiscard]] int     last_layer_count() const       { return m_session.common.dirty_telemetry.last_layer_count; }

    // ── RenderRuntime forwarders (OOL — dereferencing m_runtime's class
    //    here would require <runtime/render_runtime.hpp> as a non-local
    //    include, exceeding the boundary gate's 6-include budget).
    [[nodiscard]] renderer::SoftwareRegistry& software_registry();
    [[nodiscard]] const renderer::SoftwareRegistry& software_registry() const;
    [[nodiscard]] graph::GraphNodeCatalog& graph_node_registry();
    [[nodiscard]] const graph::GraphNodeCatalog& graph_node_registry() const;
    [[nodiscard]] effects::EffectCatalog& effect_catalog();
    [[nodiscard]] const effects::EffectCatalog& effect_catalog() const;
    [[nodiscard]] cache::FramebufferPool& software_framebuffer_pool();
    [[nodiscard]] const cache::FramebufferPool& software_framebuffer_pool() const;
    [[nodiscard]] std::shared_ptr<cache::FramebufferPool> framebuffer_pool();
    [[nodiscard]] RenderCounters* counters()                             { return &m_counters; }
    [[nodiscard]] const RenderCounters* counters() const                 { return &m_counters; }
    [[nodiscard]] chronon3d::ExecutionScheduler& scheduler() noexcept;
    [[nodiscard]] const chronon3d::ExecutionScheduler& scheduler() const noexcept;
    [[nodiscard]] runtime::RenderRuntime& runtime() noexcept;
    [[nodiscard]] const runtime::RenderRuntime& runtime() const noexcept;
    [[nodiscard]] bool has_runtime() const noexcept { return m_runtime != nullptr; }  // moved-from gate (runtime() would deref null)
    [[nodiscard]] FontEngine& font_engine();
    [[nodiscard]] const FontEngine& font_engine() const;
    [[nodiscard]] graph::CompiledGraphCache& graph_cache();
    [[nodiscard]] const graph::CompiledGraphCache& graph_cache() const;
    [[nodiscard]] const Config& config() const                            { return m_config; }
    [[nodiscard]] Config& config()                                        { return m_config; }
    [[nodiscard]] graph::RenderBackend& backend();
    [[nodiscard]] const graph::RenderBackend& backend() const;

    // ── Fase 3 — pre-loaded text render resources ────────────────────
    [[nodiscard]] TextRenderResources* text_render_resources();
    [[nodiscard]] const TextRenderResources* text_render_resources() const;

    // ── Session access ─────────────────────────────────────────────────
    [[nodiscard]] RenderSession& session()                       { return m_session.common; }
    [[nodiscard]] const RenderSession& session() const           { return m_session.common; }
    [[nodiscard]] SoftwareRenderSession& software_session()      { return m_session; }
    [[nodiscard]] const SoftwareRenderSession& software_session() const { return m_session; }
    [[nodiscard]] FrameHistory& frame_history()                  { return m_session.common.frame_history; }
    [[nodiscard]] const FrameHistory& frame_history() const      { return m_session.common.frame_history; }
    [[nodiscard]] DirtyHistory& dirty_telemetry()                { return m_session.common.dirty_telemetry; }
    [[nodiscard]] const DirtyHistory& dirty_telemetry() const    { return m_session.common.dirty_telemetry; }
    [[nodiscard]] chronon3d::graph::SceneHasher& scene_hasher()  { return m_session.common.scene_hasher(); }
    [[nodiscard]] const chronon3d::graph::SceneHasher& scene_hasher() const { return m_session.common.scene_hasher(); }
    [[nodiscard]] chronon3d::graph::SceneProgramStore& program_store()      { return m_session.common.program_store(); }
    [[nodiscard]] const chronon3d::graph::SceneProgramStore& program_store() const { return m_session.common.program_store(); }
    [[nodiscard]] RendererBufferRing& buffer_ring()               { return m_session.software.buffer_ring; }
    [[nodiscard]] const RendererBufferRing& buffer_ring() const   { return m_session.software.buffer_ring; }
    [[nodiscard]] TransformScratchBuffer& scratch_buffer()        { return m_session.software.scratch_buffer; }
    [[nodiscard]] const TransformScratchBuffer& scratch_buffer() const { return m_session.software.scratch_buffer; }
    [[nodiscard]] cache::NodeCache& node_cache() noexcept;
    [[nodiscard]] const cache::NodeCache& node_cache() const noexcept;

    // ── Convenience methods for graph pipeline orchestration ──────────
    void mark_fast_path_reused(Frame frame, const Camera2_5D& cam, uint64_t combined_fp);
    void commit_frame_state(Frame, const Camera2_5D&, uint64_t, uint64_t, uint64_t, uint64_t,
                            std::unordered_map<std::string, LayerBBoxState>&&);
    void commit_prev_frame_state(Frame, const Camera2_5D&, uint64_t, uint64_t, uint64_t, uint64_t,
                                 std::unordered_map<std::string, LayerBBoxState>&&);
    void update_dirty_telemetry(bool rect_enabled, std::optional<raster::BBox> rect,
                                bool tile_execution_used, bool fast_path_reused,
                                bool graph_reused);

private:
    Config            m_config;
    RenderSettings    m_settings{};
    RenderCounters    m_counters;
    ImageRenderer     m_image_renderer;
    std::shared_ptr<media::MediaFrameProvider> m_video_decoder;
    std::shared_ptr<image::ImageBackend> m_image_backend;
    const CompositionRegistry* m_registry{nullptr};
    runtime::RenderRuntime* m_runtime{nullptr};
    std::unique_ptr<runtime::RenderRuntime> m_owned_runtime_storage;
#ifdef CHRONON3D_ENABLE_TEXT
    std::unique_ptr<FontEngine> m_font_engine;
#endif
    // Fase 4 — SoftwareRegistry now owned directly by SoftwareRenderer,
    // no longer forwarded through RenderRuntime.
    std::unique_ptr<TextRenderResources> m_text_render_resources;
    std::unique_ptr<renderer::SoftwareRegistry> m_software_registry;
    SoftwareRenderSession m_session;
};

}  // namespace chronon3d
