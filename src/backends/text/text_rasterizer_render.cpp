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

/// Apply a `Fill` to `ctx` as either fill or stroke, delegating to the
/// shared `blend2d_bridge::paint::build_bl_gradient` for gradient
/// construction.  `op` discriminates `setFillStyle` vs `setStrokeStyle`
/// so the legacy duplication between `apply_text_fill_style` and
/// `apply_text_stroke_style` (~90 lines each, textual mirror images)
/// collapses to one helper.
inline void apply_text_style(
    BLContext& ctx,
    chronon3d::blend2d_bridge::paint::StyleOp op,
    const std::optional<Fill>& style_opt,
    const Color& fallback_color,
    float origin_x,
    float origin_y,
    float width,
    float height
) {
    auto set = [&](auto&& value) {
        if (op == chronon3d::blend2d_bridge::paint::StyleOp::Fill)
            ctx.setFillStyle(std::forward<decltype(value)>(value));
        else
            ctx.setStrokeStyle(std::forward<decltype(value)>(value));
    };

    if (!style_opt.has_value()) {
        set(to_bl_rgba(fallback_color));
        return;
    }
    const Fill& fill = *style_opt;
    if (fill.type == FillType::Solid) {
        set(to_bl_rgba(fill.solid));
        return;
    }
    if (auto gradient = build_bl_gradient(fill, origin_x, origin_y, width, height)) {
        set(*gradient);
        return;
    }
    set(to_bl_rgba(fallback_color));
}

/// Thin wrappers preserving the legacy call-site signature.  Both will
/// be removed once all callers are migrated to `apply_text_style`.
inline void apply_text_fill_style(
    BLContext& ctx,
    const TextStyle& style,
    const Color& fallback_color,
    float origin_x,
    float origin_y,
    float width,
    float height
) {
    apply_text_style(ctx,
        chronon3d::blend2d_bridge::paint::StyleOp::Fill,
        style.paint.fill_style, fallback_color,
        origin_x, origin_y, width, height);
}

inline void apply_text_stroke_style(
    BLContext& ctx,
    const TextStyle& style,
    const Color& fallback_stroke_color,
    float origin_x,
    float origin_y,
    float width,
    float height
) {
    apply_text_style(ctx,
        chronon3d::blend2d_bridge::paint::StyleOp::Stroke,
        style.paint.stroke_style, fallback_stroke_color,
        origin_x, origin_y, width, height);
}

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

/// Converts a PlacedGlyphRun to Blend2D's BLGlyphRun for fillGlyphRun().
/// The struct must outlive the BLGlyphRun (which holds raw pointers into
/// the vectors).  Keep the struct alive through the rendering call.
///
/// Uses BL_GLYPH_PLACEMENT_TYPE_ADVANCE_OFFSET: placement is a relative
/// offset from the current pen position, and after each glyph the pen is
/// advanced by `advance`.  We therefore use the raw HarfBuzz relative
/// offsets (x_offset / y_offset), NOT the cumulative x / y positions.
///
/// IMPORTANT: fillGlyphRun with ADVANCE_OFFSET expects placement values
/// in FONT DESIGN UNITS, not pixels.  HarfBuzz provides values in pixels
/// (26.6 fixed-point ÷ 64).  This conversion multiplies by (upem / font_size)
/// to convert pixels to design units, matching Blend2D's expectations.
///
/// The tracking-aware advances come pre-computed from PlacedGlyphRun,
/// so this conversion handles field copies + design-unit scaling.
struct HbToBlGlyphRun {
    std::vector<uint32_t> glyph_ids;
    std::vector<BLGlyphPlacement> placements;
    BLGlyphRun bl_run{};

    /// Convert a PlacedGlyphRun into a Blend2D BLGlyphRun.
    /// Uses the raw offsets (x_offset, y_offset) and tracking-aware
    /// advances from the PlacedGlyphRun, matching the ADVANCE_OFFSET
    /// placement type.  Scales pixel values to font design units.
    static HbToBlGlyphRun from(const PlacedGlyphRun& placed,
                                const BLFontFace& face,
                                float font_size) {
        // Convert pixel values to font design units (Blend2D's
        // ADVANCE_OFFSET expects design units, not pixels).
        const double upem = static_cast<double>(face.unitsPerEm());
        const double scale = (upem > 0.0 && font_size > 0.0f)
            ? upem / static_cast<double>(font_size) : 1.0;
        HbToBlGlyphRun result;
        result.glyph_ids.reserve(placed.glyphs.size());
        result.placements.reserve(placed.glyphs.size());
        for (const auto& pg : placed.glyphs) {
            result.glyph_ids.push_back(pg.glyph_id);
            BLGlyphPlacement p;
            // Use raw offsets (not cumulative x/y), matching
            // BL_GLYPH_PLACEMENT_TYPE_ADVANCE_OFFSET semantics.
            p.placement.reset(pg.x_offset * scale, pg.y_offset * scale);
            p.advance.reset(pg.advance_x * scale, pg.advance_y * scale);
            result.placements.push_back(p);
        }
        result.bl_run.glyphData = result.glyph_ids.data();
        result.bl_run.glyphAdvance = int8_t(sizeof(uint32_t));
        result.bl_run.placementData = result.placements.data();
        result.bl_run.placementAdvance = int8_t(sizeof(BLGlyphPlacement));
        result.bl_run.placementType = BL_GLYPH_PLACEMENT_TYPE_ADVANCE_OFFSET;
        result.bl_run.size = result.glyph_ids.size();
        return result;
    }
};

#ifdef CHRONON3D_ENABLE_TEXT
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

    BLPath build_path(const PlacedGlyphRun& placed, float origin_x, float origin_y) {
        BLPath path;
        if (!ft_face || placed.glyphs.empty()) return path;

        std::lock_guard<std::mutex> lock(mutex);

        constexpr float kScale = 1.0f / 64.0f;
        constexpr double kConicWeight = 1.0;

        struct DecomposeCtx {
            BLPath* path;
            double  off_x;
            double  off_y;
            bool    first_contour{true};
        };

        FT_Outline_Funcs funcs;
        funcs.move_to = [](const FT_Vector* to, void* user) -> int {
            auto* ctx = static_cast<DecomposeCtx*>(user);
            if (!ctx->first_contour) ctx->path->close();
            ctx->first_contour = false;
            ctx->path->moveTo(
                ctx->off_x + static_cast<double>(to->x) * kScale,
                ctx->off_y - static_cast<double>(to->y) * kScale);
            return 0;
        };
        funcs.line_to = [](const FT_Vector* to, void* user) -> int {
            auto* ctx = static_cast<DecomposeCtx*>(user);
            ctx->path->lineTo(
                ctx->off_x + static_cast<double>(to->x) * kScale,
                ctx->off_y - static_cast<double>(to->y) * kScale);
            return 0;
        };
        funcs.conic_to = [](const FT_Vector* control, const FT_Vector* to, void* user) -> int {
            auto* ctx = static_cast<DecomposeCtx*>(user);
            ctx->path->conicTo(
                ctx->off_x + static_cast<double>(control->x) * kScale,
                ctx->off_y - static_cast<double>(control->y) * kScale,
                ctx->off_x + static_cast<double>(to->x) * kScale,
                ctx->off_y - static_cast<double>(to->y) * kScale,
                kConicWeight);
            return 0;
        };
        funcs.cubic_to = [](const FT_Vector* c1, const FT_Vector* c2, const FT_Vector* to, void* user) -> int {
            auto* ctx = static_cast<DecomposeCtx*>(user);
            ctx->path->cubicTo(
                ctx->off_x + static_cast<double>(c1->x) * kScale,
                ctx->off_y - static_cast<double>(c1->y) * kScale,
                ctx->off_x + static_cast<double>(c2->x) * kScale,
                ctx->off_y - static_cast<double>(c2->y) * kScale,
                ctx->off_x + static_cast<double>(to->x) * kScale,
                ctx->off_y - static_cast<double>(to->y) * kScale);
            return 0;
        };
        funcs.delta = 0;
        funcs.shift = 0;

        for (const auto& pg : placed.glyphs) {
            if (FT_Load_Glyph(ft_face, pg.glyph_id, FT_LOAD_NO_BITMAP) != 0) continue;
            if (ft_face->glyph->format != FT_GLYPH_FORMAT_OUTLINE) continue;

            {
                const float glyph_ox = origin_x + pg.x;
                const float glyph_oy = origin_y + pg.y;

                DecomposeCtx ctx{&path, static_cast<double>(glyph_ox), static_cast<double>(glyph_oy), true};
                FT_Outline_Decompose(&ft_face->glyph->outline, &funcs, &ctx);
                if (!ctx.first_contour) path.close();
            }
        }
        return path;
    }
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
    FontEngine* font_engine,
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

    // WP-8 PR 8.0 — local_engine fallback uses typed_resolver_for_deep_code
    // (PR 8.0 transition bridge; deleted in PR 8.1).  Production callers
    // must supply `font_engine` explicitly via the renderer.runtime().resolver()
    // path; this local fallback handles the legacy code paths that constructed
    // a RenderNode without binding one at creation time.
    //
    // TODO(WP-8 PR 8.1): hoist the local `FontEngine` into a per-renderer
    // member (e.g. `SoftwareRenderer::font_engine_`) initialised from
    // `sw_renderer->runtime().resolver()` so it's not rebuilt each call.
    // Cost today: each `rasterize_text_to_bl_image` constructs a brand-new
    // FontEngine (FT_Library init + face cache wipe).  Correct, but wasteful.
    FontEngine local_engine{chronon3d::runtime::typed_resolver_for_deep_code()};
    FontEngine* engine = font_engine ? font_engine : &local_engine;

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
    // On atlas hit: blit cached glyph bitmaps (skip fillGlyphRun).
    // On miss: render normally, then store glyphs for future reuse.
    // Only for solid-color fills without geometric transforms, box
    // backgrounds, or strokes (ensures clean glyph extraction).
    struct PendingGlyphStore {
        std::string font_path;
        PlacedGlyphRun placed;
        BLFont font;
        float origin_x{0.0f}, origin_y{0.0f};
        float font_size{0.0f};
        u32   fill_rgba{0};
    };
    std::vector<PendingGlyphStore> pending_glyph_stores;

    auto can_use_glyph_atlas = [&](
        const TextStyle& style, bool has_placed, bool has_stroke
    ) -> bool {
        return has_placed
            && !use_geometric_transform
            && !t.style.box_style.enabled
            && !has_stroke
            && (!style.paint.fill_style.has_value()
                || style.paint.fill_style->type == FillType::Solid);
    };

    auto resolve_atlas_fill_rgba = [](
        const TextStyle& style, const Color& fallback
    ) -> u32 {
        if (style.paint.fill_style.has_value()
            && style.paint.fill_style->type == FillType::Solid)
            return to_bl_rgba(style.paint.fill_style->solid).value;
        return to_bl_rgba(fallback).value;
    };

    auto try_atlas_blit = [&](
        const PlacedGlyphRun& run, const std::string& fp,
        float fs, u32 fill_rgba, float ox, float oy
    ) -> bool {
        if (run.glyphs.empty()) return false;
        const u32 fsu = static_cast<u32>(std::ceil(fs));
        std::vector<GlyphAtlasEntry> entries;
        entries.reserve(run.glyphs.size());
        for (const auto& pg : run.glyphs) {
            auto e = glyph_atlas_lookup(fp, pg.glyph_id, fsu);
            if (!e || e->fill_color_rgba != fill_rgba) return false;
            entries.push_back(std::move(*e));
        }
        for (size_t i = 0; i < run.glyphs.size(); ++i) {
            const auto& pg = run.glyphs[i];
            const auto& e  = entries[i];
            ctx.blitImage(
                BLPoint(ox + pg.x + static_cast<float>(e.x_offset),
                        oy + pg.y + static_cast<float>(e.y_offset)),
                *e.image);
        }
        return true;
    };

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
                    BLPath stroke_path = ft_path.build_path(*placed, lx, baseline_y);
                    if (!stroke_path.empty()) {
                        ctx.strokePath(stroke_path);
                    }
                }
            } else {
                // Fallback when HarfBuzz shaping is unavailable
                ctx.strokeUtf8Text(BLPoint(lx, baseline_y), run_font, run.text.c_str());
            }
        }

        apply_text_fill_style(
            ctx, run_style, run_fill,
            text_start_x, text_start_y,
            std::max(1.0f, run.width),
            line.baseline + line.descent
        );

        if (placed) {
            const bool has_stroke = run_style.paint.stroke_enabled
                && run_style.paint.stroke_width > 0.0f;
            if (can_use_glyph_atlas(run_style, true, has_stroke)) {
                const u32 frgba = resolve_atlas_fill_rgba(run_style, run_fill);
                if (try_atlas_blit(*placed, run_style.font_path,
                        run_shape_size, frgba, lx, baseline_y)) {
                    if (profiling::g_current_counters)
                        profiling::g_current_counters->glyph_atlas_hits.fetch_add(1, std::memory_order_relaxed);
                    return;
                }
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
                    BLPath stroke_path = ft_path.build_path(*placed, lx, ly);
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
            if (can_use_glyph_atlas(t.style, true, has_stroke)) {
                const u32 frgba = resolve_atlas_fill_rgba(t.style, line_fill);
                if (try_atlas_blit(*placed, t.style.font_path,
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
    for (const auto& ps : pending_glyph_stores) {
        glyph_atlas_store_from_placed_run(
            ps.font_path, img, ps.placed, ps.font,
            ps.origin_x, ps.origin_y, ps.font_size, ps.fill_rgba);
    }

    // ── Trim trailing rows AND compute ink-bounds for centering ───────
    float ink_center_frac = -1.0f;
    float ink_w = 0.0f;
    int ink_left = 0, ink_right = 0, ink_top = 0, ink_bottom = 0;
    {
        BLImageData trim_data;
        if (img.getData(&trim_data) == BL_SUCCESS && trim_data.pixelData && trim_data.size.h > 0) {
            const int sw = trim_data.size.w;
            const int sh = trim_data.size.h;
            const int stride = static_cast<int>(trim_data.stride / sizeof(uint32_t));
            auto* pixels = reinterpret_cast<uint32_t*>(trim_data.pixelData);

            constexpr int kSampleStrideX = 4;
            constexpr int kSampleStrideY = 2;
            std::vector<int> row_counts(static_cast<size_t>(sh), 0);
            int max_count = 0;
            int local_ink_left = sw, local_ink_right = 0;
            int local_ink_top = sh, local_ink_bottom = 0;
            for (int y = 0; y < sh; y += kSampleStrideY) {
                for (int x = 0; x < sw; x += kSampleStrideX) {
                    if (((pixels[y * stride + x] >> 24) & 0xFF) != 0) {
                        ++row_counts[static_cast<size_t>(y)];
                        if (x < local_ink_left)   local_ink_left  = x;
                        if (x > local_ink_right)  local_ink_right = x;
                        if (y < local_ink_top)    local_ink_top    = y;
                        if (y > local_ink_bottom) local_ink_bottom = y;
                    }
                }
                if (row_counts[static_cast<size_t>(y)] > max_count)
                    max_count = row_counts[static_cast<size_t>(y)];
            }
            local_ink_left   = std::max(0, local_ink_left - (kSampleStrideX - 1));
            local_ink_right  = std::min(sw - 1, local_ink_right + kSampleStrideX - 1);
            local_ink_top    = std::max(0, local_ink_top - (kSampleStrideY - 1));
            local_ink_bottom = std::min(sh - 1, local_ink_bottom + kSampleStrideY - 1);
            for (int y = 1; y < sh; ++y) {
                if (row_counts[static_cast<size_t>(y)] == 0 &&
                    y % kSampleStrideY != 0 &&
                    row_counts[static_cast<size_t>(y - 1)] > 0) {
                    row_counts[static_cast<size_t>(y)] =
                        row_counts[static_cast<size_t>(y - 1)];
                }
            }

            if (max_count > 0) {
                const int content_threshold = std::max(5,
                    static_cast<int>(std::ceil(static_cast<float>(max_count) * 0.02f)));
                int last_content_row = -1;
                for (int y = 0; y < sh; ++y) {
                    if (row_counts[static_cast<size_t>(y)] > content_threshold) {
                        last_content_row = y;
                    }
                }

                if (!t.box.enabled) {
                    const int descender_margin = std::max(8,
                        static_cast<int>(std::ceil(layout_res.font_size * 0.25f)));
                    const int safe_bottom = std::max(last_content_row,
                        local_ink_bottom + descender_margin);
                    if (safe_bottom >= 0 && safe_bottom < sh - 1) {
                        for (int y = safe_bottom + 1; y < sh; ++y) {
                            std::fill_n(pixels + y * stride, sw, uint32_t{0});
                        }
                    }

                    const int trace_threshold = std::max(2,
                        static_cast<int>(std::ceil(static_cast<float>(max_count) * 0.005f)));
                    for (int y = sh - 1; y >= 0; --y) {
                        const int count = row_counts[static_cast<size_t>(y)];
                        if (count == 0) continue;
                        if (count > trace_threshold && y <= safe_bottom) break;
                        std::fill_n(pixels + y * stride, sw, uint32_t{0});
                    }
                }

                ink_left  = local_ink_left;
                ink_right = local_ink_right;
                ink_top   = local_ink_top;
                ink_bottom = local_ink_bottom;
                if (ink_left < ink_right) {
                    const float ink_cx = 0.5f * static_cast<float>(ink_left + ink_right);
                    ink_center_frac = ink_cx;
                    ink_w = static_cast<float>(ink_right - ink_left);
                }
            }
        }
    }

    // ── Debug: draw bounding box overlays (per-instance debug_cfg — TICKET-007) ──
    if (debug_cfg && debug_cfg->text_bbox() && !t.text.empty()) {
        BLContext dbg(img);
        dbg.setCompOp(BL_COMP_OP_SRC_OVER);

        const float pad_f = static_cast<float>(padding) * 0.5f;

        if (t.box.enabled) {
            dbg.setStrokeStyle(BLRgba32(255, 48, 48, 200));
            dbg.setStrokeWidth(2.0f);
            dbg.strokeRect(pad_f, pad_f, t.box.size.x, t.box.size.y);

            const float cx = pad_f + t.box.size.x * 0.5f;
            const float cy = pad_f + t.box.size.y * 0.5f;
            dbg.setStrokeStyle(BLRgba32(0, 255, 255, 200));
            dbg.setStrokeWidth(1.0f);
            dbg.strokeLine(cx - 14.0f, cy, cx + 14.0f, cy);
            dbg.strokeLine(cx, cy - 14.0f, cx, cy + 14.0f);
        }

        if (ink_left < ink_right) {
            const float ink_x = static_cast<float>(ink_left);
            const float ink_y = static_cast<float>(ink_top);
            const float ink_w2 = static_cast<float>(ink_right - ink_left);
            const float ink_h = static_cast<float>(ink_bottom - ink_top);
            dbg.setStrokeStyle(BLRgba32(48, 255, 48, 200));
            dbg.setStrokeWidth(1.0f);
            dbg.strokeRect(ink_x, ink_y, ink_w2, ink_h);

            const float icx = static_cast<float>(ink_left + ink_right) * 0.5f;
            const float icy = static_cast<float>(ink_top + ink_bottom) * 0.5f;
            dbg.setStrokeStyle(BLRgba32(255, 255, 48, 200));
            dbg.setStrokeWidth(1.0f);
            dbg.strokeLine(icx - 8.0f, icy, icx + 8.0f, icy);
            dbg.strokeLine(icx, icy - 8.0f, icx, icy + 8.0f);
        }

        if (!layout_res.lines.empty()) {
            const float baseline_y = text_start_y + layout_res.lines[0].position.y + font.metrics().ascent;
            dbg.setStrokeStyle(BLRgba32(48, 128, 255, 180));
            dbg.setStrokeWidth(1.0f);
            dbg.strokeLine(0.0f, baseline_y, static_cast<float>(img_w), baseline_y);
        }
        dbg.end();
    }

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
