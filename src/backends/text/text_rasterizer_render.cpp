// SPDX-License-Identifier: MIT
// ═══════════════════════════════════════════════════════════════════════════
// M1.5#11 — legacy text rasterizer TU, slimmed post utility-extraction.
//
// The legacy `rasterize_text_to_bl_image` function (M1.5#9 P1-LEGACY-TEXT-
// PIPELINE deletion scheduled in M1.5#9 Step 5) keeps its full BODY here;
// ONLY the localized utility bodies were dropped:
//
//   REMOVED (now in `src/backends/text/blend2d_glyph_conversion.{hpp,cpp}`):
//     * `apply_text_style` — unified BLContext fill/stroke style applicator
//       with `StyleOp` discriminator.
//     * `apply_text_fill_style` + `apply_text_stroke_style` thin wrappers.
//     * `HbToBlGlyphRun` struct + `from(...)` static converter
//       (HarfBuzz placed → Blend2D BLGlyphRun with design-unit scaling).
//
//   REMOVED (now in `src/backends/text/freetype_outline_conversion.{hpp,cpp}`):
//     * `FtGlyphPathBuilder::build_path` body — the FT_Outline_Funcs
//       callback setup + FT_Outline_Decompose dispatch loop (outline-only).
//
//   KEPT (this TU; deleted with M1.5#9):
//     * `Blend2DResources` + `blend2d_resources()` — legacy global cache
//       of BLFontFace (sibling to M1.5#7's per-session `BLFontFaceCache`).
//       Kept LOCAL only because the legacy function's signature doesn't
//       take a `TextRenderResources*` (AGENTS.md Cat-5 ABI-stable).
//       Will be deleted with the rest of this TU in M1.5#9 Step 5.
//     * `FtGlyphPathBuilder::load_face` — legacy FT_Face loader with the
//       `const AssetResolver&` plumbing (WP-8 PR 8.0 contract). Live
//       member state (`ft_face` + `loaded_path` + `mutex`) is kept so
//       callers can extract the FT_Face raw pointer for delegation to
//       `freetype_outline::convert_ft_outline_to_bl_path` under the
//       struct's mutex.
//
// Anti-duplication invariant (user constraint M1.5#6 + M1.5#7):
//   * NO caching logic was ADDED to the new modules — they are pure
//     conversion utilities.
//   * NO face I/O was MOVED out of this TU's caches — those caches stay
//     legacy-local until M1.5#9 Step 5 wholesale deletes them.
// ═══════════════════════════════════════════════════════════════════════════

#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <mutex>
#include <unordered_map>
#include <optional>
#include <vector>

#include <chronon3d/assets/asset_registry.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/backends/text/text_rasterizer_utils.hpp>
#include <chronon3d/backends/text/text_layout_engine.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/core/config.hpp>
// PR3: include the shared paint module via a path-relative form so the
// build picks it up regardless of the public include/ root.  The new
// header lives at src/backends/software/utils/ and is mirrored only
// locally; the convention here matches `"../../utils/blend2d_bridge.hpp"`
// used by sibling software-effect TUs.
#include "../software/utils/blend2d_paint.hpp"
#include <blend2d/gradient.h>
#include <blend2d/font.h>
#include <blend2d/path.h>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/text/glyph_atlas.hpp>

// M1.5#11 — utility extraction modules (named with explicit semantic intent).
// `blend2d_glyph_conversion` hosts the apply_text_* fill/stroke helper family
// and `HbToBlGlyphRun` data-structure converter (BOTH moved out of this TU's
// anon namespace).  `freetype_outline_conversion` hosts the `BLPath`
// outline-decoder that this TU delegates to under the legacy
// `FtGlyphPathBuilder::mutex` lock.
///
///   The two build_path() callsites below were re-shaped to:
///
///     {
///       std::lock_guard<std::mutex> ft_lock(ft_path.mutex);
///       BLPath stroke_path = ::chronon3d::convert_ft_outline_to_bl_path(
///           ft_path.ft_face, *placed, lx, baseline_y);
///       ...
///     }
///
/// matching the original synchronization window (FT_Load_Glyph +
/// FT_Outline_Decompose race against concurrent FT_Set_Pixel_Sizes).
#include "blend2d_glyph_conversion.hpp"
#include "freetype_outline_conversion.hpp"
#include "text_rasterizer_atlas.hpp"
#include "text_rasterizer_debug.hpp"
#include "text_rasterizer_trim.hpp"

#ifdef CHRONON3D_ENABLE_TEXT
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_OUTLINE_H
#endif

namespace chronon3d {

// PR3: `to_bl_rgba` and `build_bl_gradient` come from the shared
// `blend2d_bridge::paint` module; the previous 6 inline copies of
// `BLGradient gradient(values, BL_EXTEND_MODE_PAD, stops.data(),
// stops.size())` are now a single canonical implementation.
// Using-declarations live inside `namespace chronon3d` so unqualified
// callers in this TU find them through `chronon3d::to_bl_rgba` lookup
// at the right scope (consistent with `text_processor_helpers.hpp`).
using chronon3d::blend2d_bridge::paint::to_bl_rgba;
using chronon3d::blend2d_bridge::paint::build_bl_gradient;

namespace {

/// Legacy BLFontFace cache — DUPLICATES M1.5#7's per-session
/// `BLFontFaceCache` (per-session ownership lives in
/// `TextRenderResources.bl_faces`).  Kept LOCAL because the legacy
/// `rasterize_text_to_bl_image` signature doesn't take a
/// `TextRenderResources*` (AGENTS.md Cat-5 ABI-stable constraint).
/// Will be deleted with the rest of this TU in M1.5#9 Step 5.
struct Blend2DResources {
    std::unordered_map<std::string, BLFontFace> faces;
    std::mutex mutex;

    // WP-8 PR 8.0 — `resolver` is sourced by the caller
    // (`rasterize_text_to_bl_image`) per the PR 8.0 contract.  Reads of
    // `typed_resolver_for_deep_code()` are no longer permitted in this
    // deep cache.
    BLFontFace get_face(const std::string& path,
                        const chronon3d::assets::AssetResolver& resolver) {
        std::lock_guard<std::mutex> lock(mutex);
        std::string resolved_path;
        if (auto opt = resolver.resolve_lexical(path)) {
            resolved_path = opt->string();
        } else {
            resolved_path = path.empty() ? std::string{} : std::string{path};
        }
        auto it = faces.find(resolved_path);
        if (it == faces.end()) {
            BLFontFace face;
            if (face.createFromFile(resolved_path.c_str()) != BL_SUCCESS) {
                spdlog::error("Blend2D: failed to load font {} (resolved: {})", path, resolved_path);
                return BLFontFace();
            }
            faces[resolved_path] = face;
            return face;
        }
        return it->second;
    }
};

Blend2DResources& blend2d_resources() {
    static Blend2DResources resources;
    return resources;
}

#ifdef CHRONON3D_ENABLE_TEXT
/// Legacy FT_Face loader.  KEPT for the lifetime of this TU (deleted
/// with M1.5#9 Step 5).  The `build_path(...)` member that USED to live
/// here is gone — outline decoding is delegated to the new module
/// `freetype_outline::convert_ft_outline_to_bl_path`, callable through
/// the live `ft_face` pointer under the struct's `mutex`.
struct FtGlyphPathBuilder {
    FT_Face    ft_face{nullptr};
    std::string loaded_path;
    std::mutex  mutex;

    static FT_Library shared_ft_lib() {
        static FT_Library lib = [] {
            FT_Library l = nullptr;
            if (FT_Init_FreeType(&l) != 0) {
                spdlog::error("FtGlyphPathBuilder: FT_Init_FreeType failed");
            }
            return l;
        }();
        return lib;
    }

    // WP-8 PR 8.0 — `resolver` is sourced by the caller
    // (`rasterize_text_to_bl_image`) per the PR 8.0 contract.  Reads of
    // `typed_resolver_for_deep_code()` are no longer permitted in this
    // deep FreeType path-builder cache.
    bool load_face(const std::string& font_path,
                   float font_size,
                   const chronon3d::assets::AssetResolver& resolver) {
        std::lock_guard<std::mutex> lock(mutex);
        std::string resolved;
        if (auto opt = resolver.resolve_lexical(font_path)) {
            resolved = opt->string();
        } else {
            resolved = font_path.empty() ? std::string{} : std::string{font_path};
        }
        if (ft_face && resolved == loaded_path) {
            FT_Set_Pixel_Sizes(ft_face, 0, static_cast<FT_UInt>(std::ceil(font_size)));
            return true;
        }
        if (ft_face) { FT_Done_Face(ft_face); ft_face = nullptr; }
        FT_Library lib = shared_ft_lib();
        if (!lib) return false;
        if (FT_New_Face(lib, resolved.c_str(), 0, &ft_face) != 0) return false;
        FT_Set_Pixel_Sizes(ft_face, 0, static_cast<FT_UInt>(std::ceil(font_size)));
        loaded_path = resolved;
        return true;
    }

    ~FtGlyphPathBuilder() {
        std::lock_guard<std::mutex> lock(mutex);
        if (ft_face) { FT_Done_Face(ft_face); ft_face = nullptr; }
    }

    FtGlyphPathBuilder() = default;
    FtGlyphPathBuilder(const FtGlyphPathBuilder&) = delete;
    FtGlyphPathBuilder& operator=(const FtGlyphPathBuilder&) = delete;
};
#endif // CHRONON3D_ENABLE_TEXT

} // anonymous namespace

using CacheKey = u64;

CacheKey hash_text_style(const TextShape& t, float effective_size, int padding, const Mat4* transform);
bool lookup_text_cache(const CacheKey& key, std::shared_ptr<TextRasterization>& out);
void store_text_cache(const CacheKey& key, const std::shared_ptr<TextRasterization>& result);

/// `resolver` is the explicit AssetResolver sourced by the caller
/// (WP-8 PR 8.0).  In production this is `sw_renderer->runtime().resolver()`
/// for the renderer-driven path; in CLI / test / dev paths it is
/// `runtime::typed_resolver_for_deep_code()`.  Both internal caches
/// (`Blend2DResources::get_face` and `FtGlyphPathBuilder::load_face`) read
/// from this argument — no more process-global reads inside this function.
///
/// `debug_cfg` is the per-instance DebugConfig forwarded from the owning
/// RenderGraphContext / SoftwareRenderer (TICKET-007 — replaces the removed
/// process-wide `detail::g_debug_config`).  When nullptr, debug overlays
/// (text-bbox highlighting, ink bounds, baselines) are skipped.  See
/// TICKET-007 for the full propagation path.
std::optional<TextRasterization> rasterize_text_to_bl_image(
    const TextShape& t,
    float effective_size,
    int padding,
    const chronon3d::assets::AssetResolver& resolver,
    bool* cache_hit,
    const Mat4* transform,
    FontEngine& font_engine,
    const chronon3d::DebugConfig* debug_cfg
) {
#ifdef CHRONON3D_ENABLE_TEXT
    std::string font_path = t.style.font_path;
    if (t.text.empty() || font_path.empty()) return std::nullopt;

    const CacheKey key = hash_text_style(t, effective_size, padding, transform);
    {
        std::shared_ptr<TextRasterization> cached;
        if (lookup_text_cache(key, cached)) {
            if (cache_hit) *cache_hit = true;
            if (profiling::g_current_counters) {
                profiling::g_current_counters->text_cache_hits.fetch_add(1, std::memory_order_relaxed);
            }
            return *cached;
        }
    }

    if (cache_hit) *cache_hit = false;
    if (profiling::g_current_counters) {
        profiling::g_current_counters->text_cache_misses.fetch_add(1, std::memory_order_relaxed);
    }

    BLFontFace face = blend2d_resources().get_face(font_path, resolver);
    if (face.empty()) return std::nullopt;

    BLFont font;
    font.createFromFace(face, effective_size);

    TextBox layout_box = t.box;
    if (t.style.box_style.enabled && t.box.enabled) {
        layout_box.size.x = std::max(0.0f, t.box.size.x - 2.0f * t.style.box_style.padding.x);
        layout_box.size.y = std::max(0.0f, t.box.size.y - 2.0f * t.style.box_style.padding.y);
    }

    // WP-8 PR 8.1 — FontEngine is now ALWAYS supplied via the
    // per-renderer `SoftwareRenderer::font_engine()` accessor (or a
    // `node.font_engine` per-RenderNode override).  No more per-call
    // construction; no more M-3 TODO; no more deep reads of the
    // process-global `runtime::typed_resolver_for_deep_code()` bridge.
    // See software_renderer.{hpp,cpp} for the new field + init.
    FontEngine* engine = &font_engine;

    FontSpec font_spec;
    font_spec.font_path     = t.style.font_path;
    font_spec.font_family   = t.style.font_family;
    font_spec.font_weight   = t.style.font_weight;
    font_spec.font_style    = t.style.font_style;

    TextLayoutInput layout_in;
    layout_in.text = t.text;
    layout_in.style = t.style;
    layout_in.style.size = effective_size;
    layout_in.box = layout_box;
    layout_in.font_engine = engine;
    layout_in.font_spec   = font_spec;

    auto start_layout = profiling::now();
    auto layout_res = TextLayoutEngine::layout(layout_in);
    if (profiling::g_current_counters) {
        auto dur = static_cast<uint64_t>(profiling::elapsed_ms(start_layout));
        profiling::g_current_counters->text_layout_ms.fetch_add(static_cast<uint64_t>(dur), std::memory_order_relaxed);
    }

    font.createFromFace(face, layout_res.font_size);

    int tw = 0;
    int th = 0;
    if (t.box.enabled) {
        tw = static_cast<int>(std::ceil(std::max(t.box.size.x, layout_res.size.x))) + padding;
        th = static_cast<int>(std::ceil(std::max(t.box.size.y, layout_res.size.y))) + padding;
    } else {
        float box_w = layout_res.size.x;
        float box_h = layout_res.size.y;
        if (t.style.box_style.enabled) {
            box_w += 2.0f * t.style.box_style.padding.x;
            box_h += 2.0f * t.style.box_style.padding.y;
        }
        tw = static_cast<int>(std::ceil(box_w)) + padding;
        th = static_cast<int>(std::ceil(box_h)) + padding;
    }

    if (tw <= 0 || th <= 0) return std::nullopt;

    bool use_geometric_transform = false;
    float bbox_min_x = 0.0f, bbox_min_y = 0.0f;
    int img_w = tw, img_h = th;

    if (transform) {
        bool is_affine =
            std::abs((*transform)[0][2]) < 1e-5f &&
            std::abs((*transform)[1][2]) < 1e-5f &&
            std::abs((*transform)[2][0]) < 1e-5f &&
            std::abs((*transform)[2][1]) < 1e-5f &&
            std::abs((*transform)[2][2] - 1.0f) < 1e-5f &&
            std::abs((*transform)[2][3]) < 1e-5f &&
            std::abs((*transform)[3][2]) < 1e-5f;

        if (is_affine) {
            const float margin = 2.0f;
            float cx0 = static_cast<float>(padding) / 2.0f;
            float cy0 = static_cast<float>(padding) / 2.0f;
            float cx1 = static_cast<float>(tw) - static_cast<float>(padding) / 2.0f;
            float cy1 = static_cast<float>(th) - static_cast<float>(padding) / 2.0f;

            Vec4 corners[4] = {
                *transform * Vec4(cx0, cy0, 0.0f, 1.0f),
                *transform * Vec4(cx1, cy0, 0.0f, 1.0f),
                *transform * Vec4(cx1, cy1, 0.0f, 1.0f),
                *transform * Vec4(cx0, cy1, 0.0f, 1.0f),
            };

            bbox_min_x = corners[0].x;
            bbox_min_y = corners[0].y;
            float bbox_max_x = corners[0].x;
            float bbox_max_y = corners[0].y;
            for (int i = 1; i < 4; ++i) {
                bbox_min_x = std::min(bbox_min_x, corners[i].x);
                bbox_min_y = std::min(bbox_min_y, corners[i].y);
                bbox_max_x = std::max(bbox_max_x, corners[i].x);
                bbox_max_y = std::max(bbox_max_y, corners[i].y);
            }

            img_w = static_cast<int>(std::ceil(bbox_max_x - bbox_min_x + margin * 2.0f));
            img_h = static_cast<int>(std::ceil(bbox_max_y - bbox_min_y + margin * 2.0f));
            bbox_min_x -= margin;
            bbox_min_y -= margin;

            if (img_w > 0 && img_h > 0) {
                use_geometric_transform = true;
            }
        }
    }

    BLImage img(img_w, img_h, BL_FORMAT_PRGB32);
    BLContext ctx(img);
    ctx.setCompOp(BL_COMP_OP_SRC_COPY);
    ctx.setFillStyle(BLRgba32(0, 0, 0, 0));
    ctx.fillAll();

    auto start_raster = profiling::now();

    if (use_geometric_transform) {
        BLMatrix2D bl_mat;
        bl_mat.reset(
            (*transform)[0][0], (*transform)[0][1],
            (*transform)[1][0], (*transform)[1][1],
            (*transform)[3][0] - bbox_min_x,
            (*transform)[3][1] - bbox_min_y
        );
        ctx.setTransform(bl_mat);
    }

    if (t.style.box_style.enabled) {
        float rect_w = t.box.enabled ? t.box.size.x : (layout_res.size.x + 2.0f * t.style.box_style.padding.x);
        float rect_h = t.box.enabled ? t.box.size.y : (layout_res.size.y + 2.0f * t.style.box_style.padding.y);
        BLRoundRect rect(padding / 2.0f, padding / 2.0f, rect_w, rect_h, t.style.box_style.radius, t.style.box_style.radius);

        ctx.setFillStyle(to_bl_rgba(t.style.box_style.background));
        ctx.fillRoundRect(rect);

        if (t.style.box_style.border_enabled && t.style.box_style.border_width > 0.0f) {
            ctx.setStrokeWidth(t.style.box_style.border_width);
            ctx.setStrokeStyle(to_bl_rgba(t.style.box_style.border_color));
            ctx.strokeRoundRect(rect);
        }
    }

    float free_w = t.box.enabled ? (t.box.size.x - (t.style.box_style.enabled ? 2.0f * t.style.box_style.padding.x : 0.0f)) : 0.0f;
    float free_h = t.box.enabled ? (t.box.size.y - (t.style.box_style.enabled ? 2.0f * t.style.box_style.padding.y : 0.0f)) : 0.0f;
    float dx_align = 0.0f;
    float dy_align = 0.0f;
    if (t.box.enabled) {
        if (t.style.align == TextAlign::Center) {
            dx_align = (free_w - layout_res.size.x) * 0.5f;
        } else if (t.style.align == TextAlign::Right) {
            dx_align = free_w - layout_res.size.x;
        }
    }
    if (t.box.enabled && free_h > layout_res.size.y) {
        if (t.style.vertical_align == VerticalAlign::Middle) {
            dy_align = (free_h - layout_res.size.y) * 0.5f;
        } else if (t.style.vertical_align == VerticalAlign::Bottom) {
            dy_align = free_h - layout_res.size.y;
        }
    }

    float text_start_x = padding / 2.0f + (t.style.box_style.enabled ? t.style.box_style.padding.x : 0.0f) + dx_align;
    if (t.box.enabled && text_start_x < padding / 2.0f) {
        text_start_x = padding / 2.0f;
    }
    float text_start_y = padding / 2.0f + (t.style.box_style.enabled ? t.style.box_style.padding.y : 0.0f) + dy_align;

    Color fill_color = t.style.paint.fill;
    if (fill_color == Color{1.0f, 1.0f, 1.0f, 1.0f} && !(t.style.color == Color{1.0f, 1.0f, 1.0f, 1.0f})) {
        fill_color = t.style.color;
    }

    const float text_block_w = std::max(1.0f, layout_res.size.x);
    const float text_block_h = std::max(1.0f, layout_res.size.y);

    auto resolve_fill_color = [](const TextStyle& style) {
        Color resolved = style.paint.fill;
        if (resolved == Color{1.0f, 1.0f, 1.0f, 1.0f} && !(style.color == Color{1.0f, 1.0f, 1.0f, 1.0f})) {
            resolved = style.color;
        }
        return resolved;
    };

    auto resolve_font_face = [&](const TextStyle& style) {
        const std::string path = style.font_path.empty() ? t.style.font_path : style.font_path;
        return blend2d_resources().get_face(path, resolver);
    };

    // ── GlyphAtlas: per-glyph cache integration ─────────────────────
    // Extracted to detail::can_use_glyph_atlas / resolve_atlas_fill_rgba /
    // try_atlas_blit / store_pending_glyphs (text_rasterizer_atlas.hpp).
    std::vector<detail::PendingGlyphStore> pending_glyph_stores;

    auto render_run = [&](const TextLayoutLineRun& run, const TextLayoutLine& line) {
        if (run.text.empty()) return;

        TextStyle run_style = run.style;
        if (run_style.font_path.empty()) run_style.font_path = t.style.font_path;
        if (run_style.font_family.empty()) run_style.font_family = t.style.font_family;
        if (run_style.font_style.empty()) run_style.font_style = t.style.font_style;
        if (run_style.font_weight == 0) run_style.font_weight = t.style.font_weight;
        if (run_style.size <= 0.0f) run_style.size = layout_res.font_size;
        if (run_style.line_height <= 0.0f) run_style.line_height = t.style.line_height;

        BLFontFace run_face = resolve_font_face(run_style);
        if (run_face.empty()) return;

        BLFont run_font;
        run_font.createFromFace(run_face, std::max(1.0f, run_style.size));

        const float baseline_y = text_start_y + line.position.y + line.baseline;
        const float lx = text_start_x + run.position.x;
        const Color run_fill = resolve_fill_color(run_style);

        const float run_shape_size = std::max(1.0f, run_style.size);
        FontSpec run_spec;
        run_spec.font_path   = run_style.font_path;
        run_spec.font_family = run_style.font_family;
        run_spec.font_weight = run_style.font_weight;
        run_spec.font_style  = run_style.font_style;

        // ── Pre-shaped run bypass ─────────────────────────────────
        // When the style carries a pre-shaped PlacedGlyphRun (e.g. from
        // typewriter_build), skip HarfBuzz shaping entirely and use the
        // pre-resolved glyphs directly.  This preserves contextual forms
        // for Arabic, Devanagari, and other complex scripts.
        std::optional<PlacedGlyphRun> placed;
        if (run_style.pre_shaped) {
            placed = *run_style.pre_shaped;
        } else {
            auto hb_run = engine->shape_text(run.text, run_spec, run_shape_size, run_style.shaping);

            // ── Canonical PlacedGlyphRun: resolve once, share between
            // fill and stroke for identical tracking and positioning. ──
            if (hb_run && !hb_run->glyphs.empty()) {
                placed = resolve_placed_glyph_run(*hb_run, run_style.tracking, run.text);
            }
        }

        // ── Stroke: convert HarfBuzz glyphs to paths so fill and
        // stroke use identical shaped glyphs.  strokeUtf8Text would
        // re-shape with Blend2D's internal shaper and may produce
        // different GSUB substitutions for Arabic, Devanagari, etc.
        // Stroke colour supports gradients via stroke_style Fill.
        if (run_style.paint.stroke_enabled && run_style.paint.stroke_width > 0.0f) {
            apply_text_stroke_style(
                ctx,
                run_style,
                run_style.paint.stroke_color,
                text_start_x,
                text_start_y,
                std::max(1.0f, run.width),
                line.baseline + line.descent
            );
            ctx.setStrokeWidth(run_style.paint.stroke_width);
            if (placed) {
                const std::string stroke_font_path = run_style.font_path;
                FtGlyphPathBuilder ft_path;
                if (ft_path.load_face(stroke_font_path, run_shape_size, resolver)) {
                    // M1.5#11 — outline-decode delegated to the new utility
                    // module under the legacy face-loader's mutex
                    // (FT_Load_Glyph + FT_Outline_Decompose race against
                    // concurrent FT_Set_Pixel_Sizes from another thread).
                    std::lock_guard<std::mutex> ft_lock(ft_path.mutex);
                    BLPath stroke_path = ::chronon3d::convert_ft_outline_to_bl_path(
                        ft_path.ft_face, *placed, lx, baseline_y);
                    if (!stroke_path.empty()) {
                        ctx.strokePath(stroke_path);
                    }
                }
            } else {
                // Fallback when HarfBuzz shaping is unavailable
                ctx.strokeUtf8Text(BLPoint(lx, baseline_y), run_font, run.text.c_str());
            }
        }

        // M1.5#11 — `apply_text_fill_style` resolved via the
        // `blend2d_glyph_conversion.hpp` include (formerly anonymous-ns
        // local; now explicitly-named external utility).
        apply_text_fill_style(
            ctx, run_style, run_fill,
            text_start_x, text_start_y,
            std::max(1.0f, run.width),
            line.baseline + line.descent
        );

        if (placed) {
            const bool has_stroke = run_style.paint.stroke_enabled
                && run_style.paint.stroke_width > 0.0f;
            if (detail::can_use_glyph_atlas(use_geometric_transform, t.style.box_style.enabled, has_stroke, run_style.paint.fill_style)) {
                const u32 frgba = detail::resolve_atlas_fill_rgba(run_style.paint.fill_style, to_bl_rgba(run_fill).value);
                if (detail::try_atlas_blit(ctx, *placed, run_style.font_path,
                        run_shape_size, frgba, lx, baseline_y)) {
                    if (profiling::g_current_counters)
                        profiling::g_current_counters->glyph_atlas_hits.fetch_add(1, std::memory_order_relaxed);
                    return;
                }
                // M1.5#11 — `HbToBlGlyphRun::from` resolved via the
                // `blend2d_glyph_conversion.hpp` include.
                auto bl = HbToBlGlyphRun::from(*placed, run_face, run_shape_size);
                ctx.fillGlyphRun(BLPoint(lx, baseline_y), run_font, bl.bl_run);
                pending_glyph_stores.push_back({
                    run_style.font_path, std::move(*placed), run_font,
                    lx, baseline_y, run_shape_size, frgba});
                if (profiling::g_current_counters)
                    profiling::g_current_counters->glyph_atlas_misses.fetch_add(1, std::memory_order_relaxed);
            } else {
                auto bl = HbToBlGlyphRun::from(*placed, run_face, run_shape_size);
                ctx.fillGlyphRun(BLPoint(lx, baseline_y), run_font, bl.bl_run);
            }
        } else {
            ctx.fillUtf8Text(BLPoint(lx, baseline_y), run_font, run.text.c_str());
        }
    };

    for (const auto& line : layout_res.lines) {
        if (line.text.empty()) continue;

        if (line.runs.size() > 1) {
            for (const auto& run : line.runs) render_run(run, line);
            continue;
        }

        const float lx = text_start_x + line.position.x;
        const float ly = text_start_y + line.position.y + line.baseline;
        const Color line_fill = resolve_fill_color(t.style);

        // ── Pre-shaped run bypass ─────────────────────────────────
        // Use pre-shaped glyphs when available (typewriter/TextAnimator).
        std::optional<PlacedGlyphRun> placed;
        if (t.style.pre_shaped) {
            placed = *t.style.pre_shaped;
        } else {
            // ── HarfBuzz-shaped glyph rendering ─────────────────────
            FontSpec line_spec;
            line_spec.font_path   = t.style.font_path;
            line_spec.font_family = t.style.font_family;
            line_spec.font_weight = t.style.font_weight;
            line_spec.font_style  = t.style.font_style;

            // Use the resolved shaping direction from the run if available,
            // falling back to the overall style direction (Auto).  When bidi
            // segmentation is active, per-run directions (LTR/RTL) are more
            // precise than Auto-detect on the concatenated line text.
            const TextShaping& line_shaping = (!line.runs.empty())
                ? line.runs[0].style.shaping
                : t.style.shaping;
            auto hb_run = engine->shape_text(line.text, line_spec, layout_res.font_size, line_shaping);

            if (hb_run && !hb_run->glyphs.empty()) {
                placed = resolve_placed_glyph_run(*hb_run, t.style.tracking, line.text);
            }
        }

        // ── Stroke: same HarfBuzz glyph paths as fill (see render_run above).
        // Stroke colour supports gradients via stroke_style Fill.
        if (t.style.paint.stroke_enabled && t.style.paint.stroke_width > 0.0f) {
            apply_text_stroke_style(
                ctx,
                t.style,
                t.style.paint.stroke_color,
                text_start_x,
                text_start_y,
                text_block_w,
                text_block_h
            );
            ctx.setStrokeWidth(t.style.paint.stroke_width);
            if (placed) {
                const std::string stroke_font_path = t.style.font_path;
                FtGlyphPathBuilder ft_path;
                if (ft_path.load_face(stroke_font_path, layout_res.font_size, resolver)) {
                    // M1.5#11 — outline-decode delegated to the new utility
                    // module under the legacy face-loader's mutex.
                    std::lock_guard<std::mutex> ft_lock(ft_path.mutex);
                    BLPath stroke_path = ::chronon3d::convert_ft_outline_to_bl_path(
                        ft_path.ft_face, *placed, lx, ly);
                    if (!stroke_path.empty()) {
                        ctx.strokePath(stroke_path);
                    }
                }
            } else {
                ctx.strokeUtf8Text(BLPoint(lx, ly), font, line.text.c_str());
            }
        }        apply_text_fill_style(
            ctx, t.style, line_fill,
            text_start_x, text_start_y,
            text_block_w,
            text_block_h
        );

        if (placed) {
            const bool has_stroke = t.style.paint.stroke_enabled
                && t.style.paint.stroke_width > 0.0f;
            if (detail::can_use_glyph_atlas(use_geometric_transform, t.style.box_style.enabled, has_stroke, t.style.paint.fill_style)) {
                const u32 frgba = detail::resolve_atlas_fill_rgba(t.style.paint.fill_style, to_bl_rgba(line_fill).value);
                if (detail::try_atlas_blit(ctx, *placed, t.style.font_path,
                        layout_res.font_size, frgba, lx, ly)) {
                    if (profiling::g_current_counters)
                        profiling::g_current_counters->glyph_atlas_hits.fetch_add(1, std::memory_order_relaxed);
                    continue;
                }
                auto bl = HbToBlGlyphRun::from(*placed, face, layout_res.font_size);
                ctx.fillGlyphRun(BLPoint(lx, ly), font, bl.bl_run);
                pending_glyph_stores.push_back({
                    t.style.font_path, std::move(*placed), font,
                    lx, ly, layout_res.font_size, frgba});
                if (profiling::g_current_counters)
                    profiling::g_current_counters->glyph_atlas_misses.fetch_add(1, std::memory_order_relaxed);
            } else {
                auto bl = HbToBlGlyphRun::from(*placed, face, layout_res.font_size);
                ctx.fillGlyphRun(BLPoint(lx, ly), font, bl.bl_run);
            }
        } else {
            ctx.fillUtf8Text(BLPoint(lx, ly), font, line.text.c_str());
        }
    }

    ctx.end();

    // ── GlyphAtlas: store individual glyphs for future reuse ────────
    detail::store_pending_glyphs(pending_glyph_stores, img);

    // ── Trim trailing rows AND compute ink-bounds for centering ───────
    auto ink_trim = detail::trim_and_compute_ink_bounds(img, layout_res.font_size, t.box.enabled);
    const float ink_center_frac = ink_trim.ink_center_frac;
    const float ink_w = ink_trim.ink_w;
    const int ink_left = ink_trim.ink_left;
    const int ink_right = ink_trim.ink_right;
    const int ink_top = ink_trim.ink_top;
    const int ink_bottom = ink_trim.ink_bottom;

    // ── Debug: draw bounding box overlays (per-instance debug_cfg — TICKET-007) ──
    detail::draw_debug_overlays(img, debug_cfg, t, static_cast<float>(padding),
                                ink_left, ink_right, ink_top, ink_bottom,
                                layout_res, font, text_start_y, img_w);

    if (profiling::g_current_counters) {
        auto dur = static_cast<uint64_t>(profiling::elapsed_ms(start_raster));
        profiling::g_current_counters->text_rasterization_ms.fetch_add(static_cast<uint64_t>(dur), std::memory_order_relaxed);
    }

    BLGlyphBuffer gb;
    gb.setUtf8Text(t.text.c_str(), t.text.size());
    font.shape(gb);
    BLTextMetrics metrics;
    font.getTextMetrics(gb, metrics);

    float x_offset, y_offset;
    if (use_geometric_transform) {
        x_offset = bbox_min_x;
        y_offset = bbox_min_y;
    } else {
        if (t.box.enabled) {
            x_offset = -padding / 2.0f;
            if (t.style.centering_mode == TextCenteringMode::PixelInk &&
                ink_center_frac >= 0.0f && t.style.align == TextAlign::Center) {
                const float box_img_cx = static_cast<float>(img_w) * 0.5f;
                const float shift = std::round(box_img_cx - ink_center_frac);
                x_offset += shift;
            }
            else if (t.style.align == TextAlign::Right) {
                x_offset -= t.box.size.x;
            }
        } else {
            const float full_width = layout_res.size.x;
            if (t.style.align == TextAlign::Center) x_offset = -full_width * 0.5f;
            else if (t.style.align == TextAlign::Right) x_offset = -full_width;
            x_offset += metrics.boundingBox.x0 - (padding / 2.0f);
            if (t.style.centering_mode == TextCenteringMode::PixelInk &&
                ink_center_frac >= 0.0f && t.style.align == TextAlign::Center) {
                const float box_img_cx = static_cast<float>(img_w) * 0.5f;
                const float shift = std::round(box_img_cx - ink_center_frac);
                x_offset += shift;
            }
        }

        if (t.box.enabled) {
            y_offset = -padding / 2.0f;
        } else {
            y_offset = -font.metrics().ascent - (padding / 2.0f);
            if (t.style.align == TextAlign::Center) {
                y_offset += (font.metrics().ascent - font.metrics().descent) * 0.5f;
            }
        }
    }

    auto result = std::make_shared<TextRasterization>();
    result->image = std::move(img);
    result->x_offset = x_offset;
    result->y_offset = y_offset;
    result->metrics = metrics;
    result->font = font;

    store_text_cache(key, result);

    return *result;
#else
    // Text rendering disabled — return early
    (void)padding;
    (void)cache_hit;
    (void)transform;
    (void)font_engine;
    (void)debug_cfg;   // TICKET-007: keep the parameter referenced in the disabled-build branch
    return std::nullopt;
#endif // CHRONON3D_ENABLE_TEXT
}

} // namespace chronon3d
