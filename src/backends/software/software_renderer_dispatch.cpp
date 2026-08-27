// ===========================================================================
// src/backends/software/software_renderer_dispatch.cpp
//
// M1.5#12 2/4 — SoftwareRenderer dispatch surface: render entry points +
// shape/composite/effect-blur/DOF dispatch to `m_runtime->backend()`.
//
// Single source of truth for the renderer's "dispatch to backend" methods
// that do NOT touch text resources.  The text-aware `render_scene(scene,
// optional<Camera2_5D>)` overload (which auto-arms `debug_io_fence` via
// `RenderIOFenceGuard`) lives in `software_renderer_text.cpp` for
// single-responsibility cohesion — that overload is functionally
// preflight+arm-dispatch combined, not pure dispatch.
//
// Helpers `to_local_clip` and `clipped_area` are confined to the
// anonymous namespace of THIS TU because they are only used by
// `draw_node`.  Comments left intact from the pre-split incarnation.
// ===========================================================================

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/runtime/render_preparation.hpp>
#include <chronon3d/timeline/compile_evaluate.hpp>
#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <chronon3d/render_graph/pipeline/register_pipeline_nodes.hpp>
#include <chronon3d/render_graph/executor/graph_executor.hpp>
#include <chronon3d/backends/software/shape_processor.hpp>
#include <chronon3d/backends/software/software_processor_context.hpp>
#include <chronon3d/backends/software/software_registry.hpp>
#include <chronon3d/scene/model/render/render_node.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/tracing/tracing_categories.hpp>
#include <spdlog/spdlog.h>
#ifdef CHRONON3D_BUILD_DIAGNOSTICS
#include "diagnostics/layout_preview_overlay.hpp"
#endif
#include <algorithm>
#include <optional>
#include <stdexcept>

namespace chronon3d {
    namespace raster { struct BBox; }
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

void SoftwareRenderer::preflight_images(const Scene& scene) {
    for (const auto& ref : scene.asset_manifest().filter(assets::AssetKind::Image)) {
        const auto cached = m_runtime->image_cache().get_or_load(ref.path);
        if (!cached || !cached->valid()) {
            throw std::runtime_error("Render preparation failed for scene image: " + ref.path);
        }
    }
    for (const auto& layer : scene.layers()) {
        for (const auto& node : layer.nodes) {
            const ImageShape* image = nullptr;
            switch (node.shape.type()) {
                case ShapeType::Image: image = &node.shape.image(); break;
                case ShapeType::TiledImage: image = &node.shape.tiled_image().image; break;
                default: break;
            }
            if (!image || image->path.empty()) continue;
            const auto cached = m_runtime->image_cache().get_or_load(
                image->path, image->decode_options);
            if (!cached || !cached->valid()) {
                throw std::runtime_error(
                    "Render preparation failed for scene image node: " + image->path);
            }
        }
    }
}

// ── Render entry points ──────────────────────────────────────────────────────

/// Fase C3 — canonical unified pipeline.  This is the V0.2 SDK path;
/// `render_scene()` overloads are @deprecated thin wrappers.
std::shared_ptr<Framebuffer> SoftwareRenderer::render(const Composition& comp,
                                                     Frame frame) {
    if (!m_session.common.authoring_compiled_composition ||
        m_session.common.authoring_composition_identity != comp.identity() ||
        m_session.common.authoring_composition_name != comp.name()) {
        auto compiled = chronon3d::compile_composition(
            comp, CompositionCompileContext{});
        if (!compiled) {
            throw std::runtime_error(
                "Composition compilation failed for '" + comp.name() +
                "': " + compiled.error().message);
        }
        m_session.common.authoring_composition_name = comp.name();
        m_session.common.authoring_composition_identity = comp.identity();
        m_session.common.authoring_compiled_composition =
            std::make_shared<const CompiledComposition>(std::move(compiled).value());
    }
    return render_compiled(*m_session.common.authoring_compiled_composition, frame);
}

std::shared_ptr<Framebuffer> SoftwareRenderer::render_compiled(
    const CompiledComposition& compiled, Frame frame) {
    m_session.common.clear_last_frame_error();
    if (m_session.common.prepared_composition != &compiled) {
        const auto preparation = runtime::prepare_render(
            this, compiled,
            runtime::RenderPreparationOptions{
                .warmup_renderer = false,
                .reference_frame = frame,
            });
        if (!preparation.ok()) {
            throw std::runtime_error(
                "Render preparation failed for compiled composition: " +
                preparation.diagnostic());
        }
        m_session.common.prepared_composition = &compiled;
    }

    profiling::ProfilingGuard scope(&m_counters, m_runtime->framebuffer_pool_shared().get());
    return graph::render_compiled_composition_frame(
        m_runtime->backend(), node_cache(), m_settings, m_registry,
        m_video_decoder.get(), compiled, frame, this);
}

/// @deprecated Fase C3 — use render(Composition, Frame) instead.
std::shared_ptr<Framebuffer> SoftwareRenderer::render_scene(const Scene& scene,
                                                            const Camera& camera, i32 width,
                                                            i32 height, float fps) {
    m_session.common.clear_last_frame_error();
    // Cat-2 font preflight: warm both BLFontFace and FreeTypeFace caches
    // BEFORE any draw_text_run dispatch.  After this call, every
    // render-thread resolve_handle becomes a cache hit (no I/O).  The
    // I/O fence itself is opt-in (callers/test may arm with
    // trr->set_debug_io_fence(true) for defence-in-depth).
    //
    // preflight_fonts() body lives in software_renderer_text.cpp; declared
    // canonically here via the class header, so link works.
    if (auto* trr = text_render_resources(); trr && m_runtime) {
        (void)preflight_fonts(scene, m_runtime->resolver());
    }

    // This deprecated scene wrapper predates the compiled preparation
    // barrier. Keep its compatibility contract explicit: image I/O is done
    // once before graph execution, while the graph/render loop remains
    // lookup-only. The canonical composition path uses prepare_render()
    // instead and does not enter this wrapper.
    preflight_images(scene);
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
        fps,
        "scene",
        this
    );
    return res;
}

/// @deprecated Fase C3 — diagnostics-only wrapper.
std::string SoftwareRenderer::debug_render_graph(const Scene& scene, const Camera& camera,
                                                  i32 width, i32 height, float fps, Frame frame,
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
        fps
    );
}

// ── Shape dispatch ───────────────────────────────────────────────────────────

graph::RenderOpResult SoftwareRenderer::draw_node(Framebuffer& fb, const RenderNode& node,
                                                  const RenderState& state, const Camera& camera, i32 width,
                                                  i32 height) {
    CHRONON_TRACE_SCOPE("chronon.node", "draw_node");
    m_counters.pixels_touched.fetch_add(clipped_area(width, height, to_local_clip(fb, state.clip_rect)), std::memory_order_relaxed);
    // The backend owns the immutable processor snapshot and refreshes it only
    // when the registry generation changes. Creating a registry snapshot per
    // shape dispatch allocates vectors/shared ownership in the render hot path.
    const auto snapshot = m_runtime->backend().processor_snapshot();
    const auto processor = snapshot->shape_shared(snapshot->shape_handle(node.shape.type()));
    if (!processor) {
        const std::string message = "No processor registered for shape type";
        spdlog::error("{}", message);
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::InvalidInput, message});
    }
    processor->draw(make_processor_context(this), fb, node, state, camera, width, height);
#ifdef CHRONON3D_BUILD_DIAGNOSTICS
    if (m_settings.diagnostics.enabled) {
        renderer::diagnostics::draw_layout_preview(fb, node, state, *processor);
    }
#endif
    return graph::RenderOpResult(graph::RenderOpOutcome{1});
}

void SoftwareRenderer::composite_layer(Framebuffer& dst, const Framebuffer& src, BlendMode mode, const std::optional<raster::BBox>& clip, CompositeOperator op) {
    m_runtime->backend().composite_layer(dst, src, mode, clip, op);
}

void SoftwareRenderer::apply_blur(Framebuffer& fb, f32 radius, const std::optional<raster::BBox>& clip) {
    m_runtime->backend().apply_blur(fb, radius, clip);
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

} // namespace chronon3d
