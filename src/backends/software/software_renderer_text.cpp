// ===========================================================================
// src/backends/software/software_renderer_text.cpp
//
// M1.5#12 3/4 — SoftwareRenderer text surface: preflight_fonts + text-aware
// render_scene overload + text_render_resources / font_engine accessors.
//
// Single source of truth for the renderer's text-dispatch logic.  Per FASE
// B B1 / Cat-2 framing, the text-aware `render_scene(scene, optional<
// Camera2_5D>)` overload lives here because it auto-arms the
// `TextRenderResources::debug_io_fence` (Cat-2 / TICKET-087) — preflight +
// fence-arm is intrinsically text logic and may NOT be split across TUs
// (an unnamed-namespace struct cannot live in one TU and be used in
// another).  The non-text-aware `render_scene(scene, Camera)` overload
// stays in dispatch.cpp because it does not arm the fence.
//
// Anonymous-namespace `RenderIOFenceGuard` struct is the Cat-2 RAII
// helper for the atomic fence — confined to this TU because only the
// text-aware render_scene variant uses it.
// ===========================================================================

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <chronon3d/render_graph/pipeline/register_pipeline_nodes.hpp>
#include <chronon3d/render_graph/executor/graph_executor.hpp>
#include <chronon3d/backends/text/text_render_resources.hpp>
#ifdef CHRONON3D_ENABLE_TEXT
#include <chronon3d/backends/text/text_rasterizer_utils.hpp>
// WP-8 PR 8.1 — the renderer-local FontEngine is constructed from
// `m_runtime->resolver()` in `software_renderer_factory.cpp`.  This TU
// is the SOLE owner of the `font_engine()` accessor bodies (the per-renderer
// FontEngine is unique_ptr'd and we must NOT instantiate the deleter in
// more than one TU — ODR violation risk).
#include <chronon3d/text/font_engine.hpp>
#endif
// TICKET-dashboard-build: linux-dashboard-dev preset defines
// CHRONON3D_BUILD_DIAGNOSTICS=ON, which enables the draw_null_overlay()
// call below; include its header so the namespace resolves.
#ifdef CHRONON3D_BUILD_DIAGNOSTICS
#include "diagnostics/nulls_overlay.hpp"
#endif
#include <chronon3d/core/profiling/profiling.hpp>
#include <optional>
#include <set>
#include <string>
#include <utility>

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
//
// Confined to THIS TU (anonymous namespace) — only the text-aware
// render_scene variant uses it.  Duplicated in text.cpp would re-open
// Cat-5 over-engineering concerns.
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

} // namespace

// ── Text accessors (single per-renderer FontEngine + TextRenderResources) ────

TextRenderResources* SoftwareRenderer::text_render_resources() {
    return m_text_render_resources.get();
}

const TextRenderResources* SoftwareRenderer::text_render_resources() const {
    return m_text_render_resources.get();
}

// WP-8 PR 8.1 — sole owner of font_engine() accessor body.  Cannot be
// defined in another TU because unique_ptr<FontEngine>'s deleter must be
// instantiated exactly once to avoid ODR violation.
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

// ── preflight_fonts (TICKET-087 / Cat-2) ─────────────────────────────────────
//
// In-class declaration restored in software_renderer.hpp using
// `struct FontPreflightSummary;` forward-decl (gate-3 I3 fix 7 → 6
// preserved from TICKET-078).  Only the body definition lives here; the
// parent header's class declaration names `preflight_fonts` so the
// symbol is visible at call sites in dispatch.cpp's render_scene body.
//
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

// ── Text-aware render_scene overload (auto-arms debug_io_fence) ──────────────
//
// Lives here (not in dispatch.cpp) because it must arm the
// `RenderIOFenceGuard` from the Cat-2 RAII helper above.  Functionally
// combines preflight + arm-fence + render dispatch — text logic that
// the dispatch TU cannot replicate without exporting the
// `RenderIOFenceGuard` struct.
std::shared_ptr<Framebuffer> SoftwareRenderer::render_scene(
    const Scene& scene, const std::optional<Camera2_5D>& camera, i32 width, i32 height, float fps) {
    m_session.common.clear_last_frame_error();
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
        fps,
        "scene",
        this
    );
#ifdef CHRONON3D_BUILD_DIAGNOSTICS
    if (res && m_settings.diagnostics.enabled) {
        renderer::diagnostics::draw_null_overlay(*res, effective_scene, effective_scene.camera_2_5d());
    }
#endif
    return res;
}

} // namespace chronon3d
