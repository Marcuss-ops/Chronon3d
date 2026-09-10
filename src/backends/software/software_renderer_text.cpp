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
#include <chronon3d/text/glyph_atlas.hpp>
#ifdef CHRONON3D_ENABLE_TEXT
// WP-8 PR 8.1 — the renderer-local FontEngine is constructed from
// `m_runtime->resolver()` in `software_renderer_factory.cpp`.  This TU
// is the SOLE owner of the `font_engine()` accessor bodies (the per-renderer
// FontEngine is unique_ptr'd and we must NOT instantiate the deleter in
// more than one TU — ODR violation risk).
#include <chronon3d/text/font_engine.hpp>
#endif
// CHRONON3D_BUILD_DIAGNOSTICS=ON enables the draw_null_overlay()
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

// R1 — runtime owns the FontEngine; the renderer delegates to it.
// The accessor body lives here (single TU) because it dereferences
// runtime::RuntimeContext, whose complete type is available here.
FontEngine& SoftwareRenderer::font_engine() { return m_runtime->font_engine(); }
const FontEngine& SoftwareRenderer::font_engine() const { return m_runtime->font_engine(); }

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
            if (fh.valid()) {
                ++summary.preflight_succeeded;
                if (fh.bl_face) {
                    BLFont font;
                    if (font.createFromFace(*fh.bl_face, key.second) == BL_SUCCESS) {
                        for (const auto& pg : h.value->layout->placed.glyphs) {
                            if (pg.glyph_id == 0) continue;
                            const u32 fs = static_cast<u32>(std::max(1.0f, std::round(key.second)));
                            if (trr->lookup_glyph_atlas(key.first, pg.glyph_id, fs)) continue;

                            BLBoxI bounds{};
                            u32 gid = pg.glyph_id;
                            if (font.getGlyphBounds(&gid, intptr_t{0}, &bounds, 1) != BL_SUCCESS) continue;
                            constexpr double kGlyphUnit = 1.0 / 64.0;
                            const double bx0 = static_cast<double>(bounds.x0) * kGlyphUnit;
                            const double by0 = static_cast<double>(bounds.y0) * kGlyphUnit;
                            const double bx1 = static_cast<double>(bounds.x1) * kGlyphUnit;
                            const double by1 = static_cast<double>(bounds.y1) * kGlyphUnit;
                            const int x0 = static_cast<int>(std::floor(bx0));
                            const int y0 = static_cast<int>(std::floor(by0));
                            const int width = static_cast<int>(std::ceil(bx1)) - x0;
                            const int height = static_cast<int>(std::ceil(by1)) - y0;
                            if (width <= 0 || height <= 0) continue;

                            auto image = std::make_shared<BLImage>(width, height, BL_FORMAT_PRGB32);
                            BLContext context(*image);
                            context.setCompOp(BL_COMP_OP_SRC_COPY);
                            context.setFillStyle(BLRgba32(0, 0, 0, 0));
                            context.fillAll();
                            context.setCompOp(BL_COMP_OP_SRC_OVER);
                            const Color& fill = h.value->paint.fill;
                            context.setFillStyle(BLRgba32(
                                static_cast<std::uint32_t>(std::clamp(fill.r, 0.0f, 1.0f) * 255.0f),
                                static_cast<std::uint32_t>(std::clamp(fill.g, 0.0f, 1.0f) * 255.0f),
                                static_cast<std::uint32_t>(std::clamp(fill.b, 0.0f, 1.0f) * 255.0f),
                                static_cast<std::uint32_t>(std::clamp(fill.a, 0.0f, 1.0f) * 255.0f)));
                            BLGlyphPlacement placement{};
                            placement.placement.reset(0.0, 0.0);
                            placement.advance.reset(0.0, 0.0);
                            BLGlyphRun run{};
                            run.glyphData = &gid;
                            run.glyphAdvance = static_cast<int8_t>(sizeof(gid));
                            run.placementData = &placement;
                            run.placementAdvance = static_cast<int8_t>(sizeof(placement));
                            run.placementType = BL_GLYPH_PLACEMENT_TYPE_ADVANCE_OFFSET;
                            run.size = 1;
                            context.fillGlyphRun(BLPoint(-x0, -y0), font, run);
                            context.end();

                            GlyphAtlasEntry entry;
                            entry.image = std::move(image);
                            entry.x_offset = x0;
                            entry.y_offset = y0;
                            entry.advance_x = pg.advance_x;
                            entry.fill_color_rgba =
                                (static_cast<std::uint32_t>(std::clamp(fill.r, 0.0f, 1.0f) * 255.0f) << 24u) |
                                (static_cast<std::uint32_t>(std::clamp(fill.g, 0.0f, 1.0f) * 255.0f) << 16u) |
                                (static_cast<std::uint32_t>(std::clamp(fill.b, 0.0f, 1.0f) * 255.0f) << 8u) |
                                static_cast<std::uint32_t>(std::clamp(fill.a, 0.0f, 1.0f) * 255.0f);
                            trr->store_glyph_atlas(key.first, gid, fs, entry);
                        }
                    }
                }
            } else {
                ++summary.preflight_missing;
            }
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
    preflight_images(scene);

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
