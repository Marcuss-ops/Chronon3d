// ===========================================================================
// src/backends/software/software_renderer_text.cpp
//
// M1.5#12 3/4 — SoftwareRenderer text surface: preflight_fonts plus
// text_render_resources / font_engine accessors.
//
// Single source of truth for the renderer's text resource and preflight logic.
// Text resource and preflight implementations live in this TU.
//
// ===========================================================================

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <chronon3d/render_graph/pipeline/register_pipeline_nodes.hpp>
#include <chronon3d/render_graph/executor/graph_executor.hpp>
#include <chronon3d/backends/text/text_render_resources.hpp>
#include <chronon3d/text/glyph_atlas.hpp>
#ifdef CHRONON3D_ENABLE_TEXT
// WP-8 PR 8.1 — the renderer-local FontEngine is resolved through the
// explicit execution context. This TU
// is the SOLE owner of the `font_engine()` accessor bodies (the per-renderer
// FontEngine is unique_ptr'd and we must NOT instantiate the deleter in
// more than one TU — ODR violation risk).
#include <chronon3d/text/font_engine.hpp>
#endif
#include "../../render_graph/nodes/text_run/gpu_text_run.hpp"
#include <set>
#include <string>
#include <utility>

namespace chronon3d {

// ── Text accessors (single per-renderer FontEngine + TextRenderResources) ────

TextRenderResources* SoftwareRenderer::text_render_resources() {
    return m_text_render_resources.get();
}

const TextRenderResources* SoftwareRenderer::text_render_resources() const {
    return m_text_render_resources.get();
}

// R1 — the execution context borrows the runtime-owned FontEngine.
// The accessor body lives here (single TU) because it dereferences
FontEngine& SoftwareRenderer::font_engine() { return *m_execution_context.text.fonts; }
const FontEngine& SoftwareRenderer::font_engine() const { return *m_execution_context.text.fonts; }

// ── preflight_fonts (TICKET-087 / Cat-2) ─────────────────────────────────────
//
// In-class declaration restored in software_renderer.hpp using
// `struct FontPreflightSummary;` forward-decl (gate-3 I3 fix 7 → 6
// preserved from TICKET-078).  Only the body definition lives here; the
// parent header's class declaration names `preflight_fonts` so the
// symbol is visible at canonical renderer call sites.
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
            if (m_execution_context.text.assets &&
                m_execution_context.text.glyph_atlas &&
                m_execution_context.text.prepared_cache &&
                graph::text_run::can_lower_gpu_text_animation(*h.value)) {
                auto prepared = graph::text_run::prepare_mtsdf_text_run(
                    *m_execution_context.text.glyph_atlas, &resolver, *h.value);
                if (prepared && prepared->ready) {
                    m_execution_context.text.prepared_cache->store(
                        *h.value->layout, prepared);
                }
            }
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

} // namespace chronon3d
