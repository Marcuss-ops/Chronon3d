// ===========================================================================
// backends/software/software_renderer.cpp
//
// TICKET-011 — SoftwareRenderer is a thin per-instance orchestrator.
// All long-lived state lives on `m_runtime` (chronon3d::runtime::RenderRuntime);
// this file holds only per-instance wiring + caching of RenderCounters /
// RenderSettings / ImageBackend / etc.
// ===========================================================================

#include "utils/render_effects_processor.hpp"
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/software_backend.hpp>
#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <chronon3d/render_graph/pipeline/register_pipeline_nodes.hpp>
#include <chronon3d/render_graph/executor/graph_executor.hpp>

#include <chronon3d/backends/assets/image_cache.hpp>
#include <chronon3d/backends/software/shape_processor.hpp>
#include <chronon3d/backends/software/builtin_processors.hpp>
#include <chronon3d/backends/software/utils/effects/per_pixel_dof.hpp>
#ifdef CHRONON3D_ENABLE_TEXT
#include <chronon3d/backends/text/text_rasterizer_utils.hpp>
// WP-8 PR 8.1 — the renderer-local FontEngine is constructed from
// `m_runtime->resolver()` in both ctors below; the .cpp is the SOLE
// owner of the heavy FreeType + HarfBuzz machinery (header keeps the
// forward declaration only).  The unique_ptr deleter is also
// instantiated here at destruction site in ~SoftwareRenderer().
#include <chronon3d/text/font_engine.hpp>
#endif
#include <chronon3d/backends/software/text_run_processor.hpp>
#include <chronon3d/cache/cache_policy.hpp>
#include <chronon3d/cache/persistent_framebuffer_store.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/core/enum_utils.hpp>
#include <chronon3d/text/glyph_atlas.hpp>
#include <optional>
#include <chronon3d/core/profiling/profiling.hpp>
#include "processors/text/text_processor_helpers.hpp"
#include "rasterizers/path/pip.hpp"
#include "rasterizers/path/path_utils.hpp"
#ifdef CHRONON3D_BUILD_DIAGNOSTICS
#include "diagnostics/bbox_overlay.hpp"
#include "diagnostics/layout_preview_overlay.hpp"
#include "diagnostics/nulls_overlay.hpp"
#endif
#include <algorithm>
#include <utility>

namespace chronon3d {
    struct RenderNode;
    struct RenderState;
    namespace raster { struct BBox; }
}

namespace chronon3d::renderer {

void apply_blur(Framebuffer& fb, f32 radius, const std::optional<raster::BBox>& clip, int passes);
void apply_color_effects(Framebuffer& fb, const LayerEffect& effect, const std::optional<raster::BBox>& clip);
void apply_effect_stack(Framebuffer& fb, const EffectStack& stack, const effects::EffectExecutionContext& context);
}

namespace chronon3d {

namespace {

// These helpers are duplicated in software_backend.cpp and will be
// removed from here once draw_node() migrates to SoftwareBackend
// (blocked on ShapeProcessor::draw() accepting RenderBackend& instead
// of SoftwareRenderer &).

std::optional<raster::BBox> to_local_clip(const Framebuffer& fb, std::optional<raster::BBox> clip) {
    if (!clip) return std::nullopt;
    raster::BBox c = *clip;
    c.x0 -= fb.origin_x();
    c.x1 -= fb.origin_x();
    c.y0 -= fb.origin_y();
    c.y1 -= fb.origin_y();
    return c;
}

uint64_t clipped_area(int32_t width, int32_t height, const std::optional<raster::BBox>& clip) {
    uint64_t area = static_cast<uint64_t>(std::max(0, width)) * static_cast<uint64_t>(std::max(0, height));
    if (!clip) return area;
    raster::BBox local_clip = *clip;
    local_clip.clip_to(width, height);
    if (local_clip.is_empty()) return 0;
    return static_cast<uint64_t>(std::max(0, local_clip.x1 - local_clip.x0)) *
           static_cast<uint64_t>(std::max(0, local_clip.y1 - local_clip.y0));
}

} // namespace

// ── Construction ──────────────────────────────────────────────────────────────

SoftwareRenderer::SoftwareRenderer(runtime::RenderRuntime& rt, Config config)
    : m_config(std::move(config))
    , m_owned_runtime_storage{}
    , m_runtime(&rt)
{
    // Per-instance state is default-initialised.  Catalogs/registries
    // were populated by RenderRuntime::populate(); the SoftwareBackend
    // is attached externally by RenderEngine::Impl.
#ifdef CHRONON3D_ENABLE_TEXT
    // WP-8 PR 8.1 — hoist the renderer-local FontEngine here.  One
    // engine per renderer, lifetime pinned to renderer lifetime.  This
    // eliminates the M-3 `FontEngine local_engine{resolver}` fallback
    // that lived inside `rasterize_text_to_bl_image` (PR 8.0 phase 2).
    m_font_engine = std::make_unique<FontEngine>(m_runtime->resolver());
#endif
}

SoftwareRenderer::SoftwareRenderer(Config config)
    : m_config(std::move(config))
{
    // Transitional ctor: synthesise an internal RenderRuntime.  We
    // initialise m_owned_runtime_storage via move-assignment in the
    // body (after m_config is populated above).  m_runtime binds to
    // the synthesised runtime.
    m_owned_runtime_storage =
        std::make_unique<runtime::RenderRuntime>(m_config);
    m_runtime = m_owned_runtime_storage.get();
#ifdef CHRONON3D_ENABLE_TEXT
    // WP-8 PR 8.1 — see primary ctor.  The asset resolver sourced from
    // the synthesised runtime is engine-local, so this engine is the
    // sole owner for the synthesised-runtime case.
    m_font_engine = std::make_unique<FontEngine>(m_runtime->resolver());
#endif
}

SoftwareRenderer::~SoftwareRenderer() = default;

// WP-8 PR 8.1 — per-renderer FontEngine accessor.  The unique_ptr is
// non-null on text builds (initialised in both ctors) and nullptr on
// non-text builds.  Callers must be inside a CHRONON3D_HAS_BACKEND_TEXT
// block before reaching for it (software_text_processor, the rasterizer
// callsite, etc.).
#ifdef CHRONON3D_ENABLE_TEXT
FontEngine& SoftwareRenderer::font_engine() { return *m_font_engine; }
const FontEngine& SoftwareRenderer::font_engine() const { return *m_font_engine; }
#else
FontEngine& SoftwareRenderer::font_engine() {
    throw std::logic_error("SoftwareRenderer::font_engine called on a non-text build (CHRONON3D_ENABLE_TEXT=OFF)");
}
const FontEngine& SoftwareRenderer::font_engine() const {
    throw std::logic_error("SoftwareRenderer::font_engine called on a non-text build (CHRONON3D_ENABLE_TEXT=OFF)");
}
#endif

// ── Rendering ────────────────────────────────────────────────────────────────

std::shared_ptr<Framebuffer> SoftwareRenderer::render_frame(const Composition& comp,
                                                            Frame frame) {
    profiling::ProfilingGuard scope(&m_counters, m_runtime->framebuffer_pool_shared().get());

    auto res = graph::render_composition_frame(
        m_runtime->backend(), node_cache(), m_settings, m_registry, m_video_decoder.get(), comp, frame,
        this /*R3 sidecar: typed SoftwareRenderer channel for software-only state*/
    );
    return res;
}

std::shared_ptr<Framebuffer> SoftwareRenderer::render_scene(const Scene& scene,
                                                            const Camera& camera, i32 width,
                                                            i32 height) {
    profiling::ProfilingGuard scope(&m_counters, m_runtime->framebuffer_pool_shared().get());

    auto res = graph::render_scene_via_graph(
        m_runtime->backend(),
        node_cache(),
        scene,
        camera,
        width,
        height,
        0,
        0.0f,
        m_settings,
        m_registry,
        m_video_decoder.get(),
        30.0f,
        "scene",
        this /*R3 sidecar*/
    );
    return res;
}

std::shared_ptr<Framebuffer> SoftwareRenderer::render_scene(
    const Scene& scene, const std::optional<Camera2_5D>& camera, i32 width, i32 height) {
    profiling::ProfilingGuard scope(&m_counters, m_runtime->framebuffer_pool_shared().get());

    Scene effective_scene = scene.clone();
    if (camera.has_value()) {
        effective_scene.set_camera_2_5d(*camera);
    }

    Camera default_camera;
    auto res = graph::render_scene_via_graph(
        m_runtime->backend(),
        node_cache(),
        effective_scene,
        default_camera,
        width,
        height,
        0,
        0.0f,
        m_settings,
        m_registry,
        m_video_decoder.get(),
        30.0f,
        "scene",
        this /*R3 sidecar*/
    );
#ifdef CHRONON3D_BUILD_DIAGNOSTICS
    if (res && m_settings.diagnostics.enabled) {
        renderer::diagnostics::draw_null_overlay(*res, effective_scene, effective_scene.camera_2_5d());
    }
#endif
    return res;
}

std::string SoftwareRenderer::debug_render_graph(const Scene& scene, const Camera& camera,
                                                  i32 width, i32 height, Frame frame,
                                                  f32 frame_time) const {
    return graph::debug_scene_graph(
        m_runtime->backend(),
        const_cast<cache::NodeCache&>(node_cache()),
        scene,
        camera,
        width,
        height,
        frame,
        frame_time,
        m_settings,
        m_registry,
        m_video_decoder.get(),
        30.0f,
        const_cast<SoftwareRenderer*>(this) /*R3 sidecar*/
    );
}

void SoftwareRenderer::apply_blur(Framebuffer& fb, f32 radius, const std::optional<raster::BBox>& clip) {
    m_runtime->backend().apply_blur(fb, radius, clip);
}

void SoftwareRenderer::apply_effect_stack(Framebuffer& fb, const EffectStack& stack, const effects::EffectExecutionContext& context) {
    m_runtime->backend().apply_effect_stack(fb, stack, context);
}

void SoftwareRenderer::draw_node(Framebuffer& fb, const RenderNode& node,
                                 const RenderState& state, const Camera& camera, i32 width,
                                 i32 height) {
    CHRONON_ZONE_C("draw_node", trace_category::kRasterize);
    m_counters.pixels_touched.fetch_add(clipped_area(width, height, to_local_clip(fb, state.clip_rect)), std::memory_order_relaxed);
    auto* processor = software_registry().get_shape(node.shape.type());
    if (!processor) {
        spdlog::error("No processor registered for shape type");
        return;
    }
    processor->draw(*this, fb, node, state, camera, width, height);
#ifdef CHRONON3D_BUILD_DIAGNOSTICS
    if (m_settings.diagnostics.enabled) {
        renderer::diagnostics::draw_layout_preview(fb, node, state, *processor);
    }
#endif
}

void SoftwareRenderer::composite_layer(Framebuffer& dst, const Framebuffer& src, BlendMode mode, const std::optional<raster::BBox>& clip, CompositeOperator op) {
    m_runtime->backend().composite_layer(dst, src, mode, clip, op);
}

void SoftwareRenderer::apply_per_pixel_dof(
    Framebuffer& framebuffer,
    std::span<const float> depth,
    const DepthOfFieldSettings& dof,
    const LensModel& lens,
    const std::optional<raster::BBox>& clip)
{
    m_runtime->backend().apply_per_pixel_dof(framebuffer, depth, dof, lens, clip);
}

graph::RenderOpResult SoftwareRenderer::draw_text_run(
    Framebuffer& fb,
    const TextRunShape& shape,
    const Mat4& model_matrix,
    float opacity
) {
#ifdef CHRONON3D_USE_BLEND2D
    CHRONON_ZONE_C("draw_text_run", trace_category::kText);
    renderer::TextRunDrawParams params{
        .fb = fb,
        .shape = shape,
        .model_matrix = model_matrix,
        .opacity = opacity,
    };
    return renderer::draw_text_run(*this, params);
#else
    (void)fb; (void)shape; (void)model_matrix; (void)opacity;
    return graph::RenderOpResult(graph::RenderBackendError{
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "draw_text_run: Blend2D not available (CHRONON3D_USE_BLEND2D=OFF)"
    });
#endif
}

} // namespace chronon3d



// === R4 OUT-OF-LINE EXTENSION ===
//
// R4 close-out (resolves HOLD on commit 0d72a851):
//   * Every method is prefixed with `SoftwareRenderer::`.
//   * `capabilities()` is added with the matching `override` qualifier.
//   * Each body retains the EXACT logic of the pre-R4 inline body.

void SoftwareRenderer::set_settings(const RenderSettings& settings) {
    m_settings = settings;
    m_counters.program_cache_capacity.store(settings.program_cache_capacity, std::memory_order_relaxed);
    m_counters.program_cache_tune.store(settings.program_cache_tune ? 1 : 0, std::memory_order_relaxed);
}
const RenderSettings& SoftwareRenderer::settings() const { return m_settings; }

void SoftwareRenderer::set_motion_blur(MotionBlurSettings mb) { m_settings.motion_blur = mb; }
const MotionBlurSettings& SoftwareRenderer::motion_blur() const { return m_settings.motion_blur; }

void SoftwareRenderer::set_diagnostic_mode(bool enabled) { m_settings.diagnostics.enabled = enabled; }
bool SoftwareRenderer::is_diagnostic_mode() const { return m_settings.diagnostics.enabled; }

void SoftwareRenderer::clear_caches() {
    m_image_renderer.clear_cache();
#ifdef CHRONON3D_HAS_BACKEND_TEXT
    renderer::clear_text_glow_cache();
    renderer::clear_text_shadow_cache();
#endif
    node_cache().clear();
    if (auto* pool = m_runtime->framebuffer_pool_shared().get()) {
        pool->clear();
    }
    m_runtime->graph_cache() = {};
    m_session.reset_job();
}
void SoftwareRenderer::clear_node_cache() { node_cache().clear(); }
void SoftwareRenderer::reset_counters() { m_counters.reset(); }
void SoftwareRenderer::set_composition_registry(const CompositionRegistry* registry) { m_registry = registry; }
const CompositionRegistry* SoftwareRenderer::composition_registry() const { return m_registry; }

ImageRenderer& SoftwareRenderer::image_renderer() { return m_image_renderer; }
cache::NodeCache& SoftwareRenderer::node_cache() noexcept { return m_runtime->node_cache(); }
const cache::NodeCache& SoftwareRenderer::node_cache() const noexcept { return m_runtime->node_cache(); }
const RenderSettings& SoftwareRenderer::render_settings() const { return m_settings; }

void SoftwareRenderer::set_video_decoder(std::shared_ptr<media::MediaFrameProvider> decoder) {
    m_video_decoder = std::move(decoder);
}
media::MediaFrameProvider* SoftwareRenderer::video_decoder() const { return m_video_decoder.get(); }

void SoftwareRenderer::set_image_backend(std::shared_ptr<image::ImageBackend> backend) {
    m_image_backend = std::move(backend);
    m_image_renderer.set_backend(m_image_backend);
}
image::ImageBackend* SoftwareRenderer::image_backend() const { return m_image_backend.get(); }

double SoftwareRenderer::last_dirty_area_ratio() const { return m_session.common.dirty_telemetry.last_dirty_area_ratio; }
bool SoftwareRenderer::last_dirty_rect_enabled() const { return m_session.common.dirty_telemetry.last_dirty_rect_enabled; }
std::optional<raster::BBox> SoftwareRenderer::last_dirty_rect() const { return m_session.common.dirty_telemetry.last_dirty_rect; }
bool SoftwareRenderer::last_tile_execution_used() const { return m_session.common.dirty_telemetry.last_tile_execution_used; }
bool SoftwareRenderer::last_fast_path_reused() const { return m_session.common.dirty_telemetry.last_fast_path_reused; }
bool SoftwareRenderer::last_graph_reused() const { return m_session.common.dirty_telemetry.last_graph_reused; }
int SoftwareRenderer::last_layer_count() const { return m_session.common.dirty_telemetry.last_layer_count; }

const CompositionRegistry* SoftwareRenderer::composition_registry() const { return m_registry; }

renderer::SoftwareRegistry& SoftwareRenderer::software_registry() { return m_runtime->software_registry(); }
const renderer::SoftwareRegistry& SoftwareRenderer::software_registry() const { return m_runtime->software_registry(); }

graph::GraphNodeCatalog& SoftwareRenderer::graph_node_registry() { return m_runtime->graph_node_registry(); }
const graph::GraphNodeCatalog& SoftwareRenderer::graph_node_registry() const { return m_runtime->graph_node_registry(); }

effects::EffectCatalog& SoftwareRenderer::effect_catalog() { return m_runtime->effect_catalog(); }
const effects::EffectCatalog& SoftwareRenderer::effect_catalog() const { return m_runtime->effect_catalog(); }

std::shared_ptr<cache::FramebufferPool> SoftwareRenderer::framebuffer_pool() { return m_runtime->framebuffer_pool_shared(); }
cache::FramebufferPool& SoftwareRenderer::software_framebuffer_pool() { return m_runtime->framebuffer_pool(); }
const cache::FramebufferPool& SoftwareRenderer::software_framebuffer_pool() const { return m_runtime->framebuffer_pool(); }

RenderCounters* SoftwareRenderer::counters() { return &m_counters; }
const RenderCounters* SoftwareRenderer::counters() const { return &m_counters; }

graph::GraphExecutor* SoftwareRenderer::executor() { return &m_runtime->executor(); }
const graph::GraphExecutor& SoftwareRenderer::executor() const noexcept { return m_runtime->executor(); }

graph::CompiledGraphCache& SoftwareRenderer::graph_cache() { return m_runtime->graph_cache(); }
const graph::CompiledGraphCache& SoftwareRenderer::graph_cache() const { return m_runtime->graph_cache(); }

Config& SoftwareRenderer::config() { return m_config; }
const Config& SoftwareRenderer::config() const { return m_config; }

graph::RenderBackend& SoftwareRenderer::backend() { return m_runtime->backend(); }
const graph::RenderBackend& SoftwareRenderer::backend() const { return m_runtime->backend(); }

RenderSession& SoftwareRenderer::session() { return m_session.common; }
const RenderSession& SoftwareRenderer::session() const { return m_session.common; }

SoftwareRenderSession& SoftwareRenderer::software_session() { return m_session; }
const SoftwareRenderSession& SoftwareRenderer::software_session() const { return m_session; }

FrameHistory& SoftwareRenderer::frame_history() { return m_session.common.frame_history; }
const FrameHistory& SoftwareRenderer::frame_history() const { return m_session.common.frame_history; }

DirtyHistory& SoftwareRenderer::dirty_telemetry() { return m_session.common.dirty_telemetry; }
const DirtyHistory& SoftwareRenderer::dirty_telemetry() const { return m_session.common.dirty_telemetry; }

chronon3d::graph::SceneHasher& SoftwareRenderer::scene_hasher() { return m_session.common.scene_hasher(); }
const chronon3d::graph::SceneHasher& SoftwareRenderer::scene_hasher() const { return m_session.common.scene_hasher(); }

chronon3d::graph::SceneProgramStore& SoftwareRenderer::program_store() { return m_session.common.program_store(); }
const chronon3d::graph::SceneProgramStore& SoftwareRenderer::program_store() const { return m_session.common.program_store(); }

// LOOP: scheduler + runtime in const + non-const flavors
chronon3d::ExecutionScheduler& SoftwareRenderer::scheduler() noexcept { return m_runtime->scheduler(); }
const chronon3d::ExecutionScheduler& SoftwareRenderer::scheduler() const noexcept { return m_runtime->scheduler(); }

runtime::RenderRuntime& SoftwareRenderer::runtime() noexcept { return *m_runtime; }
const runtime::RenderRuntime& SoftwareRenderer::runtime() const noexcept { return *m_runtime; }

void SoftwareRenderer::mark_fast_path_reused(Frame frame, const Camera2_5D& cam, uint64_t combined_fp) {
    m_session.common.dirty_telemetry.last_dirty_area_ratio = 0.0;
    m_session.common.dirty_telemetry.last_dirty_rect_enabled = false;
    m_session.common.dirty_telemetry.last_dirty_rect = std::nullopt;
    m_session.common.dirty_telemetry.last_tile_execution_used = false;
    m_session.common.dirty_telemetry.last_fast_path_reused = true;
    m_session.common.dirty_telemetry.last_graph_reused = false;
    m_session.common.frame_history.prev_frame = frame;
    m_session.common.frame_history.prev_scene_fingerprint = combined_fp;
    m_session.common.frame_history.prev_camera = cam;
    m_session.common.frame_history.prev_camera_valid = cam.enabled;
}

void SoftwareRenderer::commit_frame_state(Frame frame, const Camera2_5D& cam, uint64_t combined_fp, uint64_t static_fp, uint64_t structure_fp, uint64_t active_at_fp, std::unordered_map<std::string, graph::LayerBBoxState>&& layer_bboxes) {
    commit_prev_frame_state(frame, cam, combined_fp, static_fp, structure_fp, active_at_fp, std::move(layer_bboxes));
}

void SoftwareRenderer::commit_prev_frame_state(Frame frame, const Camera2_5D& cam, uint64_t combined_fp, uint64_t static_fp, uint64_t structure_fp, uint64_t active_at_fp, std::unordered_map<std::string, graph::LayerBBoxState>&& layer_bboxes) {
    m_session.common.dirty_telemetry.previous_layers = std::move(layer_bboxes);
    m_session.common.frame_history.prev_frame = frame;
    m_session.common.frame_history.prev_scene_fingerprint = combined_fp;
    m_session.common.frame_history.prev_static_scene_fingerprint = static_fp;
    m_session.common.frame_history.prev_graph_structure_fingerprint = structure_fp;
    m_session.common.frame_history.prev_active_at_fingerprint = active_at_fp;
    m_session.common.frame_history.prev_camera = cam;
    m_session.common.frame_history.prev_camera_valid = cam.enabled;
}

void SoftwareRenderer::update_dirty_telemetry(bool rect_enabled, std::optional<raster::BBox> rect, bool tile_execution_used, bool fast_path_reused, bool graph_reused) {
    m_session.common.dirty_telemetry.last_dirty_rect_enabled = rect_enabled;
    m_session.common.dirty_telemetry.last_dirty_rect = rect;
    m_session.common.dirty_telemetry.last_tile_execution_used = tile_execution_used;
    m_session.common.dirty_telemetry.last_fast_path_reused = fast_path_reused;
    m_session.common.dirty_telemetry.last_graph_reused = graph_reused;
}

RendererBufferRing& SoftwareRenderer::buffer_ring() { return m_session.software.buffer_ring; }
const RendererBufferRing& SoftwareRenderer::buffer_ring() const { return m_session.software.buffer_ring; }

TransformScratchBuffer& SoftwareRenderer::scratch_buffer() { return m_session.software.scratch_buffer; }
const TransformScratchBuffer& SoftwareRenderer::scratch_buffer() const { return m_session.software.scratch_buffer; }

// capabilities() — R4 close-out addition (override retained).
graph::RenderCapabilities SoftwareRenderer::capabilities() const noexcept {
#ifdef CHRONON3D_USE_BLEND2D
    return graph::RenderCapabilities{ .text_run = true };
#else
    return graph::RenderCapabilities{ .text_run = false };
#endif
}
