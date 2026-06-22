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
        "scene"
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
    return renderer::draw_text_run(make_processor_context(this), params);
#else
    (void)fb; (void)shape; (void)model_matrix; (void)opacity;
    return graph::RenderOpResult(graph::RenderBackendError{
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "draw_text_run: Blend2D not available (CHRONON3D_USE_BLEND2D=OFF)"
    });
#endif
}

} // namespace chronon3d


