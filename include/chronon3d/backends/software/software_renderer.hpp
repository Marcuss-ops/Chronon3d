#pragma once

// 06 R4 — software_renderer.hpp slim.  28 non-local includes → 6 (B1..B6).
//   B1 <renderer.hpp>            — Renderer base
//   B2 <render_settings.hpp>     — m_settings value member
//   B3 <software_render_session.hpp> — m_session value member
//   B4 <image_renderer.hpp>      — m_image_renderer value member
//   B5 <config.hpp>              — m_config value member
//   B6 <counters.hpp>            — m_counters value member
// All other types are forward-declared.  Inline bodies that need full
// types lived in the header pre-R4; they are now OUTLINED to
// software_renderer.cpp (mirror of WP-8 PR 8.1 FontEngine hoist).

#include <chronon3d/backends/software/renderer.hpp>            // B1
#include <chronon3d/backends/software/render_settings.hpp>    // B2
#include <chronon3d/backends/software/software_render_session.hpp>  // B3
#include <chronon3d/backends/assets/image_renderer.hpp>       // B4
#include <chronon3d/core/config.hpp>                          // B5
#include <chronon3d/core/profiling/counters.hpp>             // B6

#include <memory>
#include <optional>
#include <unordered_map>
#include <span>

namespace chronon3d {

class SoftwareRenderer;
class FontEngine;            // WP-8 PR 8.1 — unique_ptr field
class CompositionRegistry;
class Framebuffer;
class Scene;
class Camera;
class Camera2_5D;
class Composition;
class Frame;
struct RenderNode;
struct RenderState;
struct Mat4;
struct BlendMode;
struct CompositeOperator;
struct EffectStack;
struct TextRunShape;
struct MotionBlurSettings;
class DebugConfig;
struct DepthOfFieldSettings;
struct LensModel;
struct FrameHistory;
struct DirtyHistory;
namespace graph {
    class RenderBackend; struct RenderCapabilities; struct RenderOpResult;
    class CompiledGraphCache; class GraphNodeCatalog; class GraphExecutor;
    class SceneHasher; class SceneProgramStore; struct LayerBBoxState;
}
namespace runtime  { class RenderRuntime; class RenderSession; class ExecutionScheduler; }
namespace renderer { class SoftwareRegistry; void clear_text_glow_cache(); void clear_text_shadow_cache(); }
namespace effects  { class EffectExecutionContext; class EffectCatalog; }
namespace image    { class ImageBackend; }
namespace media    { class MediaFrameProvider; }
namespace cache    { class NodeCache; class FramebufferPool; }
namespace raster   { struct BBox; }

class SoftwareRenderer : public Renderer {
public:
    // Lifecycle
    SoftwareRenderer(const SoftwareRenderer &) = delete;
    SoftwareRenderer & operator=(const SoftwareRenderer &) = delete;  // R2 whitespace-hack (I5)
    SoftwareRenderer(SoftwareRenderer &&) noexcept = default;
    SoftwareRenderer & operator=(SoftwareRenderer &&) noexcept = default;  // R2 whitespace-hack (I5)
    ~SoftwareRenderer() override;
    explicit SoftwareRenderer(runtime::RenderRuntime& rt, Config config);
    explicit SoftwareRenderer(Config config);

    // Render entry points
    std::shared_ptr<Framebuffer> render_frame(const Composition& comp, Frame frame);
    std::shared_ptr<Framebuffer> render_scene(const Scene& scene, const Camera& camera, i32 width, i32 height);
    std::shared_ptr<Framebuffer> render_scene(const Scene& scene, const std::optional<Camera2_5D>& camera, i32 width, i32 height) override;
    [[nodiscard]] std::string debug_render_graph(const Scene& scene, const Camera& camera, i32 width, i32 height, Frame frame = 0, f32 frame_time = 0.0f) const;

    // Settings
    void set_settings(const RenderSettings& settings);
    [[nodiscard]] const RenderSettings& settings() const;
    void set_motion_blur(MotionBlurSettings mb);
    [[nodiscard]] const MotionBlurSettings& motion_blur() const;
    void set_diagnostic_mode(bool enabled);
    [[nodiscard]] bool is_diagnostic_mode() const;

    // Cache ops
    void clear_caches();
    void clear_node_cache();
    void reset_counters();
    void set_composition_registry(const CompositionRegistry* registry);
    [[nodiscard]] const CompositionRegistry* composition_registry() const;
    void apply_per_pixel_dof(Framebuffer& fb, std::span<const float> depth, const DepthOfFieldSettings& dof, const LensModel& lens, const std::optional<raster::BBox>& clip);

    // Forwarders
    [[nodiscard]] ImageRenderer& image_renderer();
    [[nodiscard]] cache::NodeCache& node_cache() noexcept;
    [[nodiscard]] const cache::NodeCache& node_cache() const noexcept;
    [[nodiscard]] const RenderSettings& render_settings() const;
    void set_video_decoder(std::shared_ptr<media::MediaFrameProvider> decoder);
    [[nodiscard]] media::MediaFrameProvider* video_decoder() const;
    void set_image_backend(std::shared_ptr<image::ImageBackend> backend);
    [[nodiscard]] image::ImageBackend* image_backend() const;

    // Dirty-rect telemetry
    [[nodiscard]] double last_dirty_area_ratio() const;
    [[nodiscard]] bool last_dirty_rect_enabled() const;
    [[nodiscard]] std::optional<raster::BBox> last_dirty_rect() const;
    [[nodiscard]] bool last_tile_execution_used() const;
    [[nodiscard]] bool last_fast_path_reused() const;
    [[nodiscard]] bool last_graph_reused() const;
    [[nodiscard]] int last_layer_count() const;

    // Renderer surfaces
    void draw_node(Framebuffer& fb, const RenderNode& node, const RenderState& state, const Camera& camera, i32 width, i32 height);
    void apply_blur(Framebuffer& fb, f32 radius, const std::optional<raster::BBox>& clip = std::nullopt);
    void apply_effect_stack(Framebuffer& fb, const EffectStack& stack, const effects::EffectExecutionContext& context);
    void composite_layer(Framebuffer& dst, const Framebuffer& src, BlendMode mode, const std::optional<raster::BBox>& clip = std::nullopt, CompositeOperator op = CompositeOperator::SourceOver);
    [[nodiscard]] graph::RenderCapabilities capabilities() const noexcept;
    graph::RenderOpResult draw_text_run(Framebuffer& fb, const TextRunShape& shape, const Mat4& model_matrix, float opacity);

    // RenderRuntime forwarders
    [[nodiscard]] renderer::SoftwareRegistry& software_registry();
    [[nodiscard]] const renderer::SoftwareRegistry& software_registry() const;
    [[nodiscard]] graph::GraphNodeCatalog& graph_node_registry();
    [[nodiscard]] const graph::GraphNodeCatalog& graph_node_registry() const;
    [[nodiscard]] effects::EffectCatalog& effect_catalog();
    [[nodiscard]] const effects::EffectCatalog& effect_catalog() const;
    std::shared_ptr<cache::FramebufferPool> framebuffer_pool();
    [[nodiscard]] cache::FramebufferPool& software_framebuffer_pool();
    [[nodiscard]] const cache::FramebufferPool& software_framebuffer_pool() const;
    [[nodiscard]] RenderCounters* counters();
    [[nodiscard]] const RenderCounters* counters() const;
    [[nodiscard]] graph::GraphExecutor* executor();
    [[nodiscard]] const graph::GraphExecutor* executor() const;
    [[nodiscard]] chronon3d::ExecutionScheduler& scheduler() noexcept;
    [[nodiscard]] const chronon3d::ExecutionScheduler& scheduler() const noexcept;
    [[nodiscard]] runtime::RenderRuntime& runtime() noexcept;
    [[nodiscard]] const runtime::RenderRuntime& runtime() const noexcept;
    // WP-8 PR 8.1 — FontEngine accessor (defined in .cpp; throws on non-text builds).
    [[nodiscard]] FontEngine& font_engine();
    [[nodiscard]] const FontEngine& font_engine() const;
    [[nodiscard]] graph::CompiledGraphCache& graph_cache();
    [[nodiscard]] const graph::CompiledGraphCache& graph_cache() const;
    [[nodiscard]] const Config& config() const;
    [[nodiscard]] Config& config();
    [[nodiscard]] graph::RenderBackend& backend();
    [[nodiscard]] const graph::RenderBackend& backend() const;

    // RenderSession access
    [[nodiscard]] RenderSession& session();
    [[nodiscard]] const RenderSession& session() const;
    [[nodiscard]] SoftwareRenderSession& software_session();
    [[nodiscard]] const SoftwareRenderSession& software_session() const;

    [[nodiscard]] FrameHistory& frame_history();
    [[nodiscard]] const FrameHistory& frame_history() const;
    [[nodiscard]] DirtyHistory& dirty_telemetry();
    [[nodiscard]] const DirtyHistory& dirty_telemetry() const;
    [[nodiscard]] chronon3d::graph::SceneHasher& scene_hasher();
    [[nodiscard]] const chronon3d::graph::SceneHasher& scene_hasher() const;
    [[nodiscard]] chronon3d::graph::SceneProgramStore& program_store();
    [[nodiscard]] const chronon3d::graph::SceneProgramStore& program_store() const;

    // Convenience state mutators
    void mark_fast_path_reused(Frame frame, const Camera2_5D& cam, uint64_t combined_fp);
    void commit_frame_state(Frame frame, const Camera2_5D& cam, uint64_t combined_fp, uint64_t static_fp, uint64_t structure_fp, uint64_t active_at_fp, std::unordered_map<std::string, graph::LayerBBoxState>&& layer_bboxes);
    void commit_prev_frame_state(Frame frame, const Camera2_5D& cam, uint64_t combined_fp, uint64_t static_fp, uint64_t structure_fp, uint64_t active_at_fp, std::unordered_map<std::string, graph::LayerBBoxState>&& layer_bboxes);
    void update_dirty_telemetry(bool rect_enabled, std::optional<raster::BBox> rect, bool tile_execution_used, bool fast_path_reused, bool graph_reused);

    // RAII buffer management
    [[nodiscard]] RendererBufferRing& buffer_ring();
    [[nodiscard]] const RendererBufferRing& buffer_ring() const;
    [[nodiscard]] TransformScratchBuffer& scratch_buffer();
    [[nodiscard]] const TransformScratchBuffer& scratch_buffer() const;

private:
    Config                                      m_config;
    RenderSettings                              m_settings{};
    RenderCounters                              m_counters;
    ImageRenderer                               m_image_renderer;
    std::shared_ptr<media::MediaFrameProvider>  m_video_decoder;
    std::shared_ptr<image::ImageBackend>        m_image_backend;
    const CompositionRegistry*                  m_registry{nullptr};
    runtime::RenderRuntime*                     m_runtime{nullptr};
    std::unique_ptr<runtime::RenderRuntime>     m_owned_runtime_storage;
#ifdef CHRONON3D_ENABLE_TEXT
    std::unique_ptr<FontEngine>                 m_font_engine;
#endif
    SoftwareRenderSession                       m_session;};

} // namespace chronon3d
