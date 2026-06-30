// ===========================================================================
// backends/software/software_renderer.cpp
//
// TICKET-011 — SoftwareRenderer is a thin per-instance orchestrator.
// All long-lived state lives on `m_runtime` (chronon3d::runtime::RenderRuntime);
// this file holds only per-instance wiring + caching of RenderCounters /
// RenderSettings / ImageBackend / etc.
// ===========================================================================

#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/backends/software/software_registry.hpp>
#include <chronon3d/backends/software/runtime_adapter.hpp>

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
#include <chronon3d/backends/text/text_render_resources.hpp>
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
#ifdef CHRONON3D_HAS_BACKEND_TEXT
// 06 R5b — text-effect cache clear functions (clear_text_glow_cache /
// clear_text_shadow_cache) live exclusively in the text-effect TU. Pulling
// the software_text_effects.hpp header here keeps `clear_caches()`'s
// HAS_BACKEND_TEXT block self-contained without polluting the SDK header.
#include "processors/text/software_text_effects.hpp"
#endif
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

// Cat-2 RAII guard for the TextRenderResources::debug_io_fence atomic.
// Constructed with a nullptr (fence disarmed).  Public `fence` member
// lets the renderer attach the actual atomic AFTER preflight_fonts()
// has warmed both BL + FT caches, so any unforeseen cache miss surfaces
// as a deterministic std::runtime_error (see Bug #7 / Cat-2 docblock
// in text_render_resources.hpp).  Dtor symmetrically disarms via
// memory_order_release (the release-set / acquire-get pair matches
// TextRenderResources::set_debug_io_fence/is_debug_io_fence_active).
struct RenderIOFenceGuard {
    std::atomic<bool>* fence{nullptr};
    RenderIOFenceGuard() noexcept = default;
    explicit RenderIOFenceGuard(std::atomic<bool>* f) noexcept : fence(f) {}
    ~RenderIOFenceGuard() {
        if (fence) fence->store(false, std::memory_order_release);
    }
    // RAII — must not be copied / moved; a copy would double-disarm.
    RenderIOFenceGuard(const RenderIOFenceGuard&) = delete;
    RenderIOFenceGuard& operator=(const RenderIOFenceGuard&) = delete;
    RenderIOFenceGuard(RenderIOFenceGuard&&) = delete;
    RenderIOFenceGuard& operator=(RenderIOFenceGuard&&) = delete;
};

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
    , m_software_registry(std::make_unique<renderer::SoftwareRegistry>())
#ifdef CHRONON3D_ENABLE_TEXT
    , m_font_engine(std::make_unique<FontEngine>(m_runtime->resolver()))
    , m_text_render_resources(std::make_unique<TextRenderResources>())
#endif
{
    backends::software::register_builtin_processors(*m_software_registry);
}

SoftwareRenderer::SoftwareRenderer(Config config)
    : m_config(std::move(config))
{
    m_owned_runtime_storage =
        std::make_unique<runtime::RenderRuntime>(m_config);
    m_runtime = m_owned_runtime_storage.get();
    m_software_registry = std::make_unique<renderer::SoftwareRegistry>();
#ifdef CHRONON3D_ENABLE_TEXT
    m_font_engine = std::make_unique<FontEngine>(m_runtime->resolver());
    m_text_render_resources = std::make_unique<TextRenderResources>();
#endif
    backends::software::register_builtin_processors(*m_software_registry);
}

// Move ops use real rvalue-ref (no EAST-CONST hack). See header.
// Members are std::move'd into *this; the moved-from object is left
// destructible-but-empty (raw pointers nulled; unique_ptrs emptied) to
// avoid double-destruction of nested resources (m_image_renderer,
// m_session each carry shared internal mutex / pool state).
SoftwareRenderer::SoftwareRenderer(SoftwareRenderer&& other) noexcept
    : m_config(std::move(other.m_config))
    , m_settings(std::move(other.m_settings))
    , m_counters(std::move(other.m_counters))
    , m_image_renderer(std::move(other.m_image_renderer))
    , m_video_decoder(std::move(other.m_video_decoder))
    , m_image_backend(std::move(other.m_image_backend))
    , m_registry(other.m_registry)
    , m_runtime(other.m_runtime)
    , m_owned_runtime_storage(
          std::move(other.m_owned_runtime_storage))
#ifdef CHRONON3D_ENABLE_TEXT
    , m_font_engine(
          std::move(other.m_font_engine))
    , m_text_render_resources(
          std::move(other.m_text_render_resources))
#endif
    , m_software_registry(
          std::move(other.m_software_registry))
    , m_session(std::move(other.m_session))
{
    m_runtime = nullptr;
    m_registry = nullptr;
}

SoftwareRenderer&
SoftwareRenderer::operator=(SoftwareRenderer&& other) noexcept
{
    m_config        = std::move(other.m_config);
    m_settings      = std::move(other.m_settings);
    m_counters      = std::move(other.m_counters);
    m_image_renderer = std::move(other.m_image_renderer);
    m_video_decoder  = std::move(other.m_video_decoder);
    m_image_backend  = std::move(other.m_image_backend);
    m_registry       = other.m_registry;
    m_runtime        = other.m_runtime;
    m_owned_runtime_storage = std::move(other.m_owned_runtime_storage);
#ifdef CHRONON3D_ENABLE_TEXT
    m_font_engine    = std::move(other.m_font_engine);
    m_text_render_resources = std::move(other.m_text_render_resources);
#endif
    m_software_registry = std::move(other.m_software_registry);
    m_session        = std::move(other.m_session);
    other.m_runtime = nullptr;
    other.m_registry = nullptr;
    return *this;
}

SoftwareRenderer::~SoftwareRenderer() = default;

// 06 R3b — settings + diagnostics: multi-line implementations moved OOL
// from the header to drop header LOC into the I2 ≤ 200 bound.
void SoftwareRenderer::set_settings(const RenderSettings& settings) {
    m_settings = settings;
    m_counters.program_cache_capacity.store(settings.program_cache_capacity, std::memory_order_relaxed);
    m_counters.program_cache_tune.store(settings.program_cache_tune ? 1 : 0, std::memory_order_relaxed);
}

void SoftwareRenderer::set_motion_blur(MotionBlurSettings mb) {
    m_settings.motion_blur = std::move(mb);
}

void SoftwareRenderer::set_diagnostic_mode(bool enabled) {
    m_settings.diagnostics.enabled = enabled;
}

void SoftwareRenderer::reset_counters() {
    m_counters.reset();
}

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

void SoftwareRenderer::set_image_backend(std::shared_ptr<image::ImageBackend> backend) {
    m_image_backend = std::move(backend);
    m_image_renderer.set_backend(m_image_backend);
}

// 06 R3b — graph-pipeline orchestration methods: multi-line implementations
// moved OOL so the header can stay under the 200-LOC bound.
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

void SoftwareRenderer::commit_frame_state(Frame frame, const Camera2_5D& cam,
                                          uint64_t combined_fp, uint64_t static_fp,
                                          uint64_t structure_fp, uint64_t active_at_fp,
                                          std::unordered_map<std::string, LayerBBoxState>&& layer_bboxes) {
    commit_prev_frame_state(frame, cam, combined_fp, static_fp, structure_fp, active_at_fp,
                            std::move(layer_bboxes));
}

void SoftwareRenderer::commit_prev_frame_state(Frame frame, const Camera2_5D& cam,
                                               uint64_t combined_fp, uint64_t static_fp,
                                               uint64_t structure_fp, uint64_t active_at_fp,
                                               std::unordered_map<std::string, LayerBBoxState>&& layer_bboxes) {
    m_session.common.dirty_telemetry.previous_layers = std::move(layer_bboxes);
    m_session.common.frame_history.prev_frame = frame;
    m_session.common.frame_history.prev_scene_fingerprint = combined_fp;
    m_session.common.frame_history.prev_static_scene_fingerprint = static_fp;
    m_session.common.frame_history.prev_graph_structure_fingerprint = structure_fp;
    m_session.common.frame_history.prev_active_at_fingerprint = active_at_fp;
    m_session.common.frame_history.prev_camera = cam;
    m_session.common.frame_history.prev_camera_valid = cam.enabled;
}

void SoftwareRenderer::update_dirty_telemetry(bool rect_enabled, std::optional<raster::BBox> rect,
                                               bool tile_execution_used, bool fast_path_reused,
                                               bool graph_reused) {
    m_session.common.dirty_telemetry.last_dirty_rect_enabled = rect_enabled;
    m_session.common.dirty_telemetry.last_dirty_rect = rect;
    m_session.common.dirty_telemetry.last_tile_execution_used = tile_execution_used;
    m_session.common.dirty_telemetry.last_fast_path_reused = fast_path_reused;
    m_session.common.dirty_telemetry.last_graph_reused = graph_reused;
}

// 06 R3b — RenderRuntime forwarders moved out-of-line; their inline bodies
// dereference `m_runtime->X()` / `*m_runtime` which require the complete
// `runtime::RenderRuntime` type.  Pulling `<chronon3d/runtime/render_runtime.hpp>`
// into the header would exceed the boundary-gate I3 ≤ 6-include budget, so
// the implementation lives here where the type is complete (this TU already
// pulls the header via `software_renderer.hpp`'s user clients).
renderer::SoftwareRegistry& SoftwareRenderer::software_registry()             { return *m_software_registry; }
const renderer::SoftwareRegistry& SoftwareRenderer::software_registry() const { return *m_software_registry; }
graph::GraphNodeCatalog& SoftwareRenderer::graph_node_registry()             { return m_runtime->graph_node_registry(); }
const graph::GraphNodeCatalog& SoftwareRenderer::graph_node_registry() const { return m_runtime->graph_node_registry(); }
effects::EffectCatalog& SoftwareRenderer::effect_catalog()                   { return m_runtime->effect_catalog(); }
const effects::EffectCatalog& SoftwareRenderer::effect_catalog() const       { return m_runtime->effect_catalog(); }
cache::FramebufferPool& SoftwareRenderer::software_framebuffer_pool()        { return m_runtime->framebuffer_pool(); }
const cache::FramebufferPool& SoftwareRenderer::software_framebuffer_pool() const { return m_runtime->framebuffer_pool(); }
std::shared_ptr<cache::FramebufferPool> SoftwareRenderer::framebuffer_pool() { return m_runtime->framebuffer_pool_shared(); }
// Note: executor forwarder was removed.  See commit message of the
// baseline-12c295be PR for the architectural rationale.
chronon3d::ExecutionScheduler& SoftwareRenderer::scheduler() noexcept        { return m_runtime->scheduler(); }
const chronon3d::ExecutionScheduler& SoftwareRenderer::scheduler() const noexcept { return m_runtime->scheduler(); }
runtime::RenderRuntime& SoftwareRenderer::runtime() noexcept                  { return *m_runtime; }
const runtime::RenderRuntime& SoftwareRenderer::runtime() const noexcept      { return *m_runtime; }
graph::CompiledGraphCache& SoftwareRenderer::graph_cache()                   { return m_runtime->graph_cache(); }
const graph::CompiledGraphCache& SoftwareRenderer::graph_cache() const       { return m_runtime->graph_cache(); }
graph::RenderBackend& SoftwareRenderer::backend()                             { return m_runtime->backend(); }
const graph::RenderBackend& SoftwareRenderer::backend() const                 { return m_runtime->backend(); }
cache::NodeCache& SoftwareRenderer::node_cache() noexcept                     { return m_runtime->node_cache(); }
const cache::NodeCache& SoftwareRenderer::node_cache() const noexcept         { return m_runtime->node_cache(); }
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

// Cat-2 font preflight (Cat-2 framing per AGENTS.md freeze audit).
// Per-layer (fontspec) walk: for each LayerKind::Text, scan nodes for the
// first TextRunShapeHandle; collect a representative (font_path, font_size)
// pair and resolve it before render starts.
FontPreflightSummary SoftwareRenderer::preflight_fonts(
    const Scene& scene,
    const assets::AssetResolver& resolver
) {
    FontPreflightSummary summary;
    auto* trr = text_render_resources();
    if (!trr) return summary;

    std::set<std::pair<std::string, f32>> seen;
    for (const auto& layer : scene.layers()) {
        if (!layer.is_text()) continue;
        for (const auto& node : layer.nodes) {
            // TextRun discrimination moved into the Shape variant
            // (ShapeType::TextRun).  The payload accessor is now
            // node.shape.text_run_shape_handle() (returns the wrapper
            // around shared_ptr<TextRunShape>).
            if (node.shape.type() != ShapeType::TextRun) continue;
            const auto& h = node.shape.text_run_shape_handle();
            if (!h.value || !h.value->layout) continue;
            const auto key = std::make_pair(
                h.value->layout->font.font_path,
                h.value->layout->font_size);
            if (!seen.insert(key).second) continue;
            ++summary.preflight_attempted;
            const FontFaceHandle fh =
                trr->resolve_handle(key.first, key.second, resolver);
            if (fh.valid()) ++summary.preflight_succeeded;
            else            ++summary.preflight_missing;
        }
    }
    return summary;
}

std::shared_ptr<Framebuffer> SoftwareRenderer::render(const Composition& comp,
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
    // Cat-2 font preflight: warm both BLFontFace and FreeTypeFace caches
    // BEFORE any draw_text_run dispatch.  After this call, every
    // render-thread resolve_handle becomes a cache hit (no I/O).  The
    // I/O fence itself is opt-in (callers/test may arm with
    // trr->set_debug_io_fence(true) for defence-in-depth).
    if (auto* trr = text_render_resources(); trr && m_runtime) {
        (void)preflight_fonts(scene, m_runtime->resolver());
    }

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
        "scene"
    );
    return res;
}

std::shared_ptr<Framebuffer> SoftwareRenderer::render_scene(
    const Scene& scene, const std::optional<Camera2_5D>& camera, i32 width, i32 height) {
    // Cat-2 font preflight + auto-arm — same pattern as the Camera
    // overload above.  RAII guard disarms on every exit path.
    RenderIOFenceGuard fence_guard(nullptr);
    if (auto* trr = text_render_resources(); trr && m_runtime) {
        (void)preflight_fonts(scene, m_runtime->resolver());
        fence_guard.fence = &trr->debug_io_fence;
    }

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
        "scene"
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
        30.0f
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
    processor->draw(make_processor_context(this), fb, node, state, camera, width, height);
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

// NOTE: `draw_text_run` and `capabilities` are NOT implemented on
// SoftwareRenderer.  They live exclusively on `SoftwareBackend`.  After
// 06 R3b the polymorphic graph::RenderBackend* is held by SoftwareBackend,
// so callers reach these methods through `sw_renderer.backend()`.

// ── Fase 3 — pre-loaded text render resources ────────────────────────────

TextRenderResources* SoftwareRenderer::text_render_resources() {
    return m_text_render_resources.get();
}

const TextRenderResources* SoftwareRenderer::text_render_resources() const {
    return m_text_render_resources.get();
}

} // namespace chronon3d


