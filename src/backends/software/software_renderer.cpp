#include "utils/render_effects_processor.hpp"
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <chronon3d/render_graph/executor/graph_executor.hpp>
#include <chronon3d/backends/software/software_compositor.hpp>
#include <chronon3d/backends/software/text_run_processor.hpp>

#include <chronon3d/backends/assets/image_cache.hpp>
#include <chronon3d/backends/software/shape_processor.hpp>
#include <chronon3d/backends/software/builtin_processors.hpp>
#include <chronon3d/backends/software/utils/effects/per_pixel_dof.hpp>
#include <chronon3d/backends/text/text_rasterizer_utils.hpp>
#include <chronon3d/cache/cache_policy.hpp>
#include <chronon3d/cache/persistent_framebuffer_store.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/core/enum_utils.hpp>
#include <chronon3d/text/glyph_atlas.hpp>
#include <optional>
#include <chronon3d/core/profiling/profiling.hpp>
#include "../../backends/software/processors/text/text_processor_helpers.hpp"
#include "../../backends/software/rasterizers/path/pip.hpp"
#include "../../backends/software/rasterizers/path/path_utils.hpp"
#ifdef CHRONON3D_BUILD_DIAGNOSTICS
#include "diagnostics/bbox_overlay.hpp"
#include "diagnostics/layout_preview_overlay.hpp"
#include "diagnostics/nulls_overlay.hpp"
#endif
#include <algorithm>

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

std::optional<raster::BBox> to_local_clip(const Framebuffer& fb, std::optional<raster::BBox> clip) {
    if (!clip) {
        return std::nullopt;
    }

    raster::BBox c = *clip;
    c.x0 -= fb.origin_x();
    c.x1 -= fb.origin_x();
    c.y0 -= fb.origin_y();
    c.y1 -= fb.origin_y();
    return c;
}

uint64_t clipped_area(int32_t width, int32_t height, const std::optional<raster::BBox>& clip) {
    uint64_t area = static_cast<uint64_t>(std::max(0, width)) * static_cast<uint64_t>(std::max(0, height));
    if (!clip) {
        return area;
    }

    raster::BBox local_clip = *clip;
    local_clip.clip_to(width, height);
    if (local_clip.is_empty()) {
        return 0;
    }
    return static_cast<uint64_t>(std::max(0, local_clip.x1 - local_clip.x0)) *
           static_cast<uint64_t>(std::max(0, local_clip.y1 - local_clip.y0));
}

} // namespace

SoftwareRenderer::SoftwareRenderer()
    : m_runtime_resources{
        .software_registry = std::make_unique<renderer::SoftwareRegistry>(),
        .executor = std::make_unique<graph::GraphExecutor>(
            Config::get().scheduler().pin_main_thread())
    }
    , m_cache_state{
        .node_cache = cache::NodeCache{
            Config::get().cache().node_cache_max_bytes()},
        .framebuffer_pool = std::make_shared<cache::FramebufferPool>(
            Config::get().cache().fb_pool_max_bytes())
    } {
    // ── Thread sub-configs to singleton / static-state components ────
    const auto& cfg = Config::get();
    const auto& cache_cfg = cfg.cache();
    const auto& sched_cfg = cfg.scheduler();

    cache::PersistentFramebufferStore::set_store_config(
        cache_cfg.disable_persistent_framebuffer_cache(),
        cfg.paths().persistent_framebuffer_cache_dir());
    ImageCache::set_capacity_bytes(cache_cfg.image_cache_max_bytes());
    cache::set_global_cache_config(cache_cfg);
    set_glyph_atlas_capacity(cache_cfg.glyph_atlas_max_bytes());
    set_text_cache_capacity(cache_cfg.text_cache_max_bytes());
    renderer::set_shadow_cache_capacity(cache_cfg.shadow_cache_max_bytes());
    renderer::set_glow_cache_capacity(cache_cfg.glow_cache_max_bytes());
    renderer::set_pip_mode(sched_cfg.pip_mode());
    renderer::set_prefetch_enabled(sched_cfg.prefetch_enabled());

    renderer::register_builtin_processors(*m_runtime_resources.software_registry);
}

SoftwareRenderer::~SoftwareRenderer() = default;

graph::GraphExecutor* SoftwareRenderer::executor() {
    return m_runtime_resources.executor.get();
}

const graph::GraphExecutor* SoftwareRenderer::executor() const {
    return m_runtime_resources.executor.get();
}

std::shared_ptr<Framebuffer> SoftwareRenderer::render_frame(const Composition& comp,
                                                            Frame frame) {
    profiling::ProfilingGuard scope(&m_counters, m_cache_state.framebuffer_pool.get());

    auto res = graph::render_composition_frame(
        *this, m_cache_state.node_cache, m_settings, m_registry, m_video_decoder.get(), comp, frame
    );
    return res;
}

std::shared_ptr<Framebuffer> SoftwareRenderer::render_scene(const Scene& scene,
                                                            const Camera& camera, i32 width,
                                                            i32 height) {
    profiling::ProfilingGuard scope(&m_counters, m_cache_state.framebuffer_pool.get());

    auto res = graph::render_scene_via_graph(
        *this,
        m_cache_state.node_cache,
        scene,
        camera,
        width,
        height,
        0,
        0.0f,
        m_settings,
        m_registry,
        m_video_decoder.get()
    );
    return res;
}

std::shared_ptr<Framebuffer> SoftwareRenderer::render_scene(
    const Scene& scene, const std::optional<Camera2_5D>& camera, i32 width, i32 height) {
    profiling::ProfilingGuard scope(&m_counters, m_cache_state.framebuffer_pool.get());

    Scene effective_scene = scene;
    if (camera.has_value()) {
        effective_scene.set_camera_2_5d(*camera);
    }

    Camera default_camera;
    auto res = graph::render_scene_via_graph(
        *this,
        m_cache_state.node_cache,
        effective_scene,
        default_camera,
        width,
        height,
        0,
        0.0f,
        m_settings,
        m_registry,
        m_video_decoder.get()
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
        const_cast<SoftwareRenderer&>(*this),
        m_cache_state.node_cache,
        scene,
        camera,
        width,
        height,
        frame,
        frame_time,
        m_settings,
        m_registry,
        m_video_decoder.get()
    );
}

void SoftwareRenderer::apply_blur(Framebuffer& fb, f32 radius, const std::optional<raster::BBox>& clip) {
    const auto local_clip = to_local_clip(fb, clip);
    m_counters.blur_pixels.fetch_add(clipped_area(fb.width(), fb.height(), local_clip), std::memory_order_relaxed);
    CHRONON_ZONE_C("apply_blur", trace_category::kEffect);
    renderer::apply_blur(fb, radius, local_clip);
}

void SoftwareRenderer::apply_effect_stack(Framebuffer& fb, const EffectStack& stack, const effects::EffectExecutionContext& context) {
    CHRONON_ZONE_C("apply_effect_stack", trace_category::kEffect);

    const auto local_clip = context.clip.has_value()
        ? to_local_clip(fb, context.clip)
        : std::nullopt;
    const uint64_t area = clipped_area(fb.width(), fb.height(), local_clip);
    for (const auto& effect : stack) {
        if (effect.enabled && effect.effect_type == effects::EffectType::Blur) {
            m_counters.blur_pixels.fetch_add(area, std::memory_order_relaxed);
        }
    }

    // Rebuild context with local (fb-relative) clip
    effects::EffectExecutionContext local_context = context;
    local_context.clip = local_clip;
    renderer::apply_effect_stack(fb, stack, local_context);
}

void SoftwareRenderer::draw_node(Framebuffer& fb, const RenderNode& node,
                                 const RenderState& state, const Camera& camera, i32 width,
                                 i32 height) {
    CHRONON_ZONE_C("draw_node", trace_category::kRasterize);
    m_counters.pixels_touched.fetch_add(clipped_area(width, height, to_local_clip(fb, state.clip_rect)), std::memory_order_relaxed);
    auto* processor = software_registry().get_shape(node.shape.type);
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
    m_counters.layers_rendered.fetch_add(1, std::memory_order_relaxed);
    CHRONON_ZONE_C("composite_layer", trace_category::kComposite);
    m_counters.pixels_touched.fetch_add(clipped_area(dst.width(), dst.height(), to_local_clip(dst, clip)), std::memory_order_relaxed);
    SoftwareCompositor::composite_layer(dst, src, mode, clip, op, m_settings.force_scalar_normal_blend);
}

void SoftwareRenderer::apply_per_pixel_dof(
    Framebuffer& framebuffer,
    std::span<const float> depth,
    const DepthOfFieldSettings& dof,
    const LensModel& lens,
    const std::optional<raster::BBox>& clip)
{
    std::vector<float> depth_vec(depth.begin(), depth.end());
    renderer::apply_per_pixel_dof(framebuffer, depth_vec, dof, lens, clip);
}

graph::RenderOpResult SoftwareRenderer::draw_text_run(
    Framebuffer& fb,
    const TextRunShape& shape,
    const Mat4& model_matrix,
    float opacity,
    bool diagnostic_mode
) {
    CHRONON_ZONE_C("draw_text_run", trace_category::kText);
    renderer::TextRunDrawParams params{
        .fb = fb,
        .shape = shape,
        .model_matrix = model_matrix,
        .opacity = opacity,
        .diagnostic_mode = diagnostic_mode,
    };
    return renderer::draw_text_run(*this, params);
}

} // namespace chronon3d
