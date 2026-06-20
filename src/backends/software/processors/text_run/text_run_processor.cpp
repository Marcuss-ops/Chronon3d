#include <chronon3d/backends/software/text_run_processor.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/shape_processor.hpp>
#include <chronon3d/backends/text/text_rasterizer_utils.hpp>  // apply_text_material
#include <chronon3d/assets/asset_registry.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <blend2d.h>
#include <blend2d/font.h>
#include <blend2d/path.h>
#include <glm/gtc/matrix_transform.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <mutex>

#include "../../utils/blend2d_bridge.hpp"
#include "../../utils/blend2d_paint.hpp"  // PR3: canonical to_bl_rgba + build_bl_gradient

#ifdef CHRONON3D_ENABLE_TEXT
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_OUTLINE_H
#endif

namespace chronon3d::renderer {

// Forward-declared in detail:: header; brought into scope for this TU.
using detail::bucket_radius_for_tier;

// PR3: adopt the canonical `to_bl_rgba` from `blend2d_bridge::paint`.
// Placed inside `namespace chronon3d::renderer` so unqualified callers
// from `draw_text_run` (and the lambdas it hosts) find it through
// normal lookup.  We do NOT place this inside the anonymous namespace
// below — anonymous namespaces do not propagate names to surrounding
// scope, so calls from code in `chronon3d::renderer` would not
// otherwise resolve.
using chronon3d::blend2d_bridge::paint::to_bl_rgba;

namespace {

// ── Blend2D resource cache (shared across text runs) ──────────────────
// NOTE: This duplicates blend2d_resources() from text_rasterizer_render.cpp
// because that cache is in an anonymous namespace there.  Future refactor
// could extract it to a shared header.

struct TextRunBlResources {
    std::unordered_map<std::string, BLFontFace> faces;
    std::mutex mutex;

    BLFontFace get_face(const std::string& path) {
        std::lock_guard<std::mutex> lock(mutex);
        const std::string resolved = resolve_asset_path(path);
        auto it = faces.find(resolved);
        if (it == faces.end()) {
            BLFontFace face;
            if (face.createFromFile(resolved.c_str()) != BL_SUCCESS) {
                spdlog::error("TextRun: failed to load font {}", resolved);
                return BLFontFace();
            }
            faces[resolved] = face;
            return face;
        }
        return it->second;
    }
};

TextRunBlResources& text_run_bl_resources() {
    static TextRunBlResources resources;
    return resources;
}

// ── Portable PI ──────────────────────────────────────────────────────

constexpr double kPi = 3.14159265358979323846;

// ── Glyph matrix builder ─────────────────────────────────────────────
//
// Builds a 2D affine matrix for a single glyph following the
// After Effects transform order:
//   T(layout_pos) × T(position) × T(anchor) × R(rotationZ) × Skew × S(scale) × T(-anchor)
//
// Skew axis controls the direction of the skew (0° = X-axis, 90° = Y-axis).

[[nodiscard]] BLMatrix2D build_glyph_matrix(const GlyphInstanceState& g) {
    const double lx = static_cast<double>(g.layout_position.x);
    const double ly = static_cast<double>(g.layout_position.y);
    const double px = static_cast<double>(g.position.x);
    const double py = static_cast<double>(g.position.y);
    // 2.5D extension: depth (Z translation) → uniform scale attenuation.
    const double pz = static_cast<double>(g.position.z);
    const double ax = static_cast<double>(g.anchor.x);
    const double ay = static_cast<double>(g.anchor.y);

    // Combined uniform scale: scale.z multiplies both X and Y.  Negative
    // scale.z flips the glyph (mirror); 0 collapses it.
    const double sz_raw_x = static_cast<double>(g.scale.x) * static_cast<double>(g.scale.z);
    const double sz_raw_y = static_cast<double>(g.scale.y) * static_cast<double>(g.scale.z);

    // Rotation Z in radians (degrees → radians)
    const double rz_rad = static_cast<double>(g.rotation.z) * (kPi / 180.0);
    const double cos_a = std::cos(rz_rad);
    const double sin_a = std::sin(rz_rad);

    // 2.5D rotations: emulate 3D orientation in a 2D rasterizer via
    // pure-affine shears.  rotation.y rotates around the Y axis (horizontal
    // hinge): glyph edges tilt left/right, simulated as horizontal shear
    // proportional to vertical position.  rotation.x rotates around the X
    // axis (vertical hinge): simulated as vertical shear proportional to
    // horizontal position.  These are not true perspective but match the
    // visual feel of classic 2.5D systems.
    const double ry_rad = static_cast<double>(g.rotation.y) * (kPi / 180.0);
    const double rx_rad = static_cast<double>(g.rotation.x) * (kPi / 180.0);
    // Clamp angle magnitude to |pi/2 - 0.01| so tan() doesn't blow up; tan
    // moves from -inf to +inf across +/-90° so any input within +/-89.95°
    // gives a usable finite shear.
    const double ry_clamped = std::clamp(ry_rad, -kPi / 2.0 + 1e-3, kPi / 2.0 - 1e-3);
    const double rx_clamped = std::clamp(rx_rad, -kPi / 2.0 + 1e-3, kPi / 2.0 - 1e-3);
    const double horiz_shear = std::tan(ry_clamped);
    const double vert_shear  = std::tan(rx_clamped);

    // Depth-based perspective: positive z = further away = smaller glyph
    // (friendly: AE convention "Z negative = closer").  Use a simple
    // attenuation model: depth_factor = 1 / (1 + |z| * 0.001).  At
    // z=0 the glyph stays full-size; at z=1000 the glyph is ~50%.
    const double depth_factor = (std::isfinite(pz) && pz > 0.0)
        ? 1.0 / (1.0 + pz * 0.001)
        : 1.0;

    // Skew (existing 2D shear framing)
    const double skew_rad = static_cast<double>(g.skew) * (kPi / 180.0);
    const double skew_axis_rad = static_cast<double>(g.skew_axis) * (kPi / 180.0);
    const double tan_skew = std::tan(skew_rad);
    const double cos_axis = std::cos(skew_axis_rad);
    const double sin_axis = std::sin(skew_axis_rad);

    // Build the matrix step by step, starting with identity.
    BLMatrix2D m;
    m.reset();

    // 1. Translate to layout position
    m.translate(lx, ly);

    // 1.5. Apply baseline shift (animator-driven nudges per AE convention):
    //      positive pixels = drop below baseline (subscript),
    //      negative pixels = raise above baseline (superscript).
    //      Applied BEFORE the position offset so that .position() stacks
    //      on top of the baseline-shifted glyph rather than replacing it.
    m.translate(0.0, static_cast<double>(g.baseline_shift));

    // 2. Translate by animated position (x, y)
    m.translate(px, py);

    // 3. Apply depth-based uniform scaling for positive z.
    //    Negative z = "closer to camera" — no shrinkage.
    if (std::abs(depth_factor - 1.0) > 1e-9) {
        m.scale(depth_factor, depth_factor);
    }

    // 4. Translate to anchor
    m.translate(ax, ay);

    // 5. Compose: scale → 2.5D shears (horiz/vert) → skew → rotation Z
    BLMatrix2D srt;
    srt.reset(sz_raw_x, 0.0, 0.0, sz_raw_y, 0.0, 0.0);   // start with scale

    // 5a. 2.5D shears — apply on top of scale.
    //     horiz_shear: x' = x + horiz_shear * y  (rotation.y)
    //     vert_shear:  y' = y + vert_shear  * x  (rotation.x)
    if (std::abs(horiz_shear) > 1e-9) {
        BLMatrix2D sh;
        sh.reset(1.0, horiz_shear, 0.0, 1.0, 0.0, 0.0);
        srt.transform(sh);
    }
    if (std::abs(vert_shear) > 1e-9) {
        BLMatrix2D sh;
        sh.reset(1.0, 0.0, vert_shear, 1.0, 0.0, 0.0);
        srt.transform(sh);
    }

    // 5b. Skew along axis (existing) — R(+axis) × SkewX × R(-axis) then
    //     re-apply X/Y scale so we don't lose the 2.5D shears.
    if (std::abs(skew_rad) > 1e-9) {
        BLMatrix2D rot_to_axis;
        rot_to_axis.reset(cos_axis, sin_axis, -sin_axis, cos_axis, 0.0, 0.0);

        BLMatrix2D rot_from_axis;
        rot_from_axis.reset(cos_axis, -sin_axis, sin_axis, cos_axis, 0.0, 0.0);

        BLMatrix2D skew_x;
        skew_x.reset(1.0, 0.0, tan_skew, 1.0, 0.0, 0.0);

        BLMatrix2D skew_composed = rot_to_axis;
        skew_composed.transform(skew_x);
        skew_composed.transform(rot_from_axis);
        // Re-apply scale after skew compose so the 2.5D shears are preserved.
        BLMatrix2D scale_mat;
        scale_mat.reset(sz_raw_x, 0.0, 0.0, sz_raw_y, 0.0, 0.0);
        skew_composed.transform(scale_mat);
        srt.transform(skew_composed);
    }

    // 5c. Rotation Z applied last (post all scale/shear/skew).
    BLMatrix2D rot;
    rot.reset(cos_a, sin_a, -sin_a, cos_a, 0.0, 0.0);
    BLMatrix2D composed = rot;
    composed.transform(srt);
    m.transform(composed);

    // 6. Translate back from anchor
    m.translate(-ax, -ay);

    return m;
}

// ── Helper: HbToBlGlyphRun for a single glyph (absolute positioning) ─

struct SingleGlyphRun {
    uint32_t glyph_id;
    BLGlyphPlacement placement;
    BLGlyphRun bl_run{};

    static SingleGlyphRun from(const PlacedGlyph& pg) {
        SingleGlyphRun result;
        result.glyph_id = pg.glyph_id;
        // Identity placement — we use ctx.transform() for positioning
        result.placement.placement.reset(0.0, 0.0);
        result.placement.advance.reset(0.0, 0.0);
        result.bl_run.glyphData = &result.glyph_id;
        result.bl_run.glyphAdvance = int8_t(sizeof(uint32_t));
        result.bl_run.placementData = &result.placement;
        result.bl_run.placementAdvance = int8_t(sizeof(BLGlyphPlacement));
        result.bl_run.placementType = BL_GLYPH_PLACEMENT_TYPE_ADVANCE_OFFSET;
        result.bl_run.size = 1;
        return result;
    }
};

// ── FreeType glyph path builder (for stroke outlines) ───────────────

#ifdef CHRONON3D_ENABLE_TEXT
struct TextRunPathBuilder {
    FT_Face    ft_face{nullptr};
    std::string loaded_path;
    std::mutex  mutex;

    static FT_Library shared_ft_lib() {
        static FT_Library lib = [] {
            FT_Library l = nullptr;
            if (FT_Init_FreeType(&l) != 0) {
                spdlog::error("TextRunPathBuilder: FT_Init_FreeType failed");
            }
            return l;
        }();
        return lib;
    }

    bool load(const std::string& font_path, float font_size) {
        std::lock_guard<std::mutex> lock(mutex);
        const std::string resolved = resolve_asset_path(font_path);
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

    ~TextRunPathBuilder() {
        std::lock_guard<std::mutex> lock(mutex);
        if (ft_face) { FT_Done_Face(ft_face); ft_face = nullptr; }
    }

    TextRunPathBuilder() = default;
    TextRunPathBuilder(const TextRunPathBuilder&) = delete;
    TextRunPathBuilder& operator=(const TextRunPathBuilder&) = delete;

    BLPath build(u32 glyph_id, float origin_x, float origin_y) {
        BLPath path;
        if (!ft_face) return path;

        std::lock_guard<std::mutex> lock(mutex);
        if (FT_Load_Glyph(ft_face, glyph_id, FT_LOAD_NO_BITMAP) != 0) return path;
        if (ft_face->glyph->format != FT_GLYPH_FORMAT_OUTLINE) return path;

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

        DecomposeCtx ctx{&path, static_cast<double>(origin_x), static_cast<double>(origin_y), true};
        FT_Outline_Decompose(&ft_face->glyph->outline, &funcs, &ctx);
        if (!ctx.first_contour) path.close();

        return path;
    }
};

#endif // CHRONON3D_ENABLE_TEXT

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════
// draw_text_run
// ═══════════════════════════════════════════════════════════════════════════

graph::RenderOpResult draw_text_run(
    SoftwareRenderer& renderer,
    TextRunDrawParams& params
) {
    const auto& shape = params.shape;
    if (!shape.layout || shape.glyphs.empty()) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::InvalidInput,
            "draw_text_run: empty layout or empty glyph set"
        });
    }

    const auto& layout = *shape.layout;
    const std::string& font_path = layout.font.font_path;
    if (font_path.empty()) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::InvalidInput,
            "draw_text_run: empty layout.font.font_path"
        });
    }

    CHRONON_ZONE_C("text_run_draw", trace_category::kText);

    // ── Safe Bbox Clipping ───────────────────────────────────────────
    // Pre-compute the world-space bbox of every glyph (2.5D-aware via
    // `compute_text_run_world_bbox`) and intersect with the framebuffer
    // bounds.  If the intersection is empty (text rotated far off-screen,
    // fully outside, etc.) we return false SILENTLY — no diagnostics, no
    // diagnostic insertion — so off-canvas text_run entries don't pollute
    // the per-frame log with shape-not-found errors.
    {
        const raster::BBox world_bbox =
            compute_text_run_world_bbox(shape, params.model_matrix, 0.0f);
        const raster::BBox fb_bbox{
            0, 0,
            static_cast<i32>(params.fb.width()),
            static_cast<i32>(params.fb.height())
        };
        const i32 x_lo = std::max(world_bbox.x0, fb_bbox.x0);
        const i32 x_hi = std::min(world_bbox.x1, fb_bbox.x1);
        const i32 y_lo = std::max(world_bbox.y0, fb_bbox.y0);
        const i32 y_hi = std::min(world_bbox.y1, fb_bbox.y1);
        if (x_hi <= x_lo || y_hi <= y_lo) {
            // world bbox does not intersect framebuffer at all — silent
            // success with zero items drawn (PR2: explicit outcome).
            return graph::RenderOpResult(graph::RenderOpOutcome{0});
        }
    }

    // ── Empty-framebuffer guard ────────────────────────────────────────────
    // A 0×0 or zero-dim framebuffer would let the BL bridge loop forever
    // (or divide by zero when scaling).  Treat as InvalidInput (the
    // caller's fb was malformed).
    if (params.fb.width() == 0 || params.fb.height() == 0) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::InvalidInput,
            "draw_text_run: zero-dimension framebuffer"
        });
    }

    // Load font face
    BLFontFace face = text_run_bl_resources().get_face(font_path);
    if (face.empty()) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::ExecutionFailure,
            "draw_text_run: failed to load font face for " + font_path
        });
    }

    BLFont font;
    font.createFromFace(face, layout.font_size);

    // ── Compute glyph bbox (the run's local image-local extent) ─────
    // Conservative padding accounts for blur, stroke, and 2.5D shears
    // before we know how aggressive the model_matrix will be.
    float min_x = 1e10f, min_y = 1e10f;
    float max_x = -1e10f, max_y = -1e10f;

    for (const auto& g : shape.glyphs) {
        const float gx = g.layout_position.x + g.position.x;
        const float gy = g.layout_position.y + g.position.y;
        const float pad = g.blur + g.stroke_width + 8.0f;
        min_x = std::min(min_x, gx - pad);
        min_y = std::min(min_y, gy - pad);
        max_x = std::max(max_x, gx + layout.placed.total_width /
            static_cast<float>(std::max(size_t(1), layout.placed.glyphs.size())) + pad);
        max_y = std::max(max_y, gy + layout.placed.total_height + pad);
        // Shadow offset extends the world bbox on its own; account for
        // it here so the run-local canvas has room when blur spreads.
        for (const auto& sh : shape.shadows) {
            if (!sh.enabled) continue;
            const float sh_pad = sh.blur + std::abs(sh.offset.x) + std::abs(sh.offset.y) + 8.0f;
            min_x = std::min(min_x, gx - sh_pad);
            max_x = std::max(max_x, gx + sh_pad);
            min_y = std::min(min_y, gy - sh_pad);
            max_y = std::max(max_y, gy + sh_pad);
        }
    }

    const float margin = 8.0f;
    const int img_w = std::max(1, static_cast<int>(std::ceil(max_x - min_x + margin * 2.0f)));
    const int img_h = std::max(1, static_cast<int>(std::ceil(max_y - min_y + margin * 2.0f)));
    const float offset_x = min_x - margin;
    const float offset_y = min_y - margin;

    // ── Per-glyph blur tiers ─────────────────────────────────────────
    // Glyphs are grouped into 5 tiers by their per-glyph blur value.
    // Each tier is rasterized onto its own surface, blurred individually,
    // and then composited SRC_OVER onto the final run image.  This means
    // a glyph with blur=16 does NOT smear across glyphs with blur=0.
    //
    // Tier thresholds and box-blur radii:
    //   tier 0: blur = 0     → radius 0  (no blur)
    //   tier 1: blur 1–4     → radius 2
    //   tier 2: blur 5–8     → radius 7
    //   tier 3: blur 9–16    → radius 13
    //   tier 4: blur > 16    → radius 20 (capped)
    static constexpr int kBlurTierRadii[5] = { 0, 2, 7, 13, 20 };
    static constexpr int kNumBlurTiers = 5;

    auto classify_blur_tier = [](float blur) -> int {
        if (blur <= 0.0f) return 0;
        if (blur <= 4.0f) return 1;
        if (blur <= 8.0f) return 2;
        if (blur <= 16.0f) return 3;
        return 4;
    };

    // Keep bucket_radius for shadow passes (shadows composite per-shadow
    // so they don't need tiering — each shadow has a uniform blur radius).
    auto bucket_radius = [](float r) -> int {
        if (r <= 0.0f)  return 0;
        if (r <= 4.0f)  return kBlurTierRadii[1];
        if (r <= 8.0f)  return kBlurTierRadii[2];
        if (r <= 16.0f) return kBlurTierRadii[3];
        return kBlurTierRadii[4];
    };

    // ── Inline separable box-blur (sliding window, O(w×h)) ──────────
    // Operates on BL_FORMAT_PRGB32 in-place.  Uses a sliding window so
    // the per-pixel cost is O(1) instead of O(radius).  Total cost is
    // roughly 2 × w × h independent of radius.
    auto apply_box_blur = [](BLImage& image, int radius) {
        if (radius <= 0) return;
        BLImageData data;
        if (image.getData(&data) != BL_SUCCESS) return;
        const int w = data.size.w;
        const int h = data.size.h;
        if (w <= 0 || h <= 0) return;
        const int stride = static_cast<int>(data.stride / sizeof(uint32_t));
        uint32_t* base = static_cast<uint32_t*>(data.pixelData);

        std::vector<uint32_t> tmp(static_cast<size_t>(w) * static_cast<size_t>(h));

        const int r = radius;
        auto unpack = [](uint32_t c, int& a, int& rr, int& g, int& b) {
            a = static_cast<int>((c >> 24) & 0xFF);
            rr = static_cast<int>((c >> 16) & 0xFF);
            g = static_cast<int>((c >>  8) & 0xFF);
            b = static_cast<int>( c        & 0xFF);
        };
        auto pack = [](int a, int rr, int g, int b) -> uint32_t {
            return (static_cast<uint32_t>(std::clamp(a, 0, 255)) << 24) |
                   (static_cast<uint32_t>(std::clamp(rr, 0, 255)) << 16) |
                   (static_cast<uint32_t>(std::clamp(g, 0, 255)) <<  8) |
                   (static_cast<uint32_t>(std::clamp(b, 0, 255)));
        };

        // Horizontal pass (sliding window): tmp[y,x] = mean of base[y, x-r..x+r].
        for (int y = 0; y < h; ++y) {
            const uint32_t* row = base + static_cast<size_t>(y) * stride;
            int sum_a = 0, sum_r = 0, sum_g = 0, sum_b = 0, count = 0;

            // Initial window: pixels [0, min(w-1, r)]
            const int init_right = std::min(w - 1, r);
            for (int k = 0; k <= init_right; ++k) {
                int pa, pr, pg, pb;
                unpack(row[k], pa, pr, pg, pb);
                sum_a += pa; sum_r += pr; sum_g += pg; sum_b += pb;
                ++count;
            }
            tmp[static_cast<size_t>(y) * w] =
                pack(sum_a / count, sum_r / count, sum_g / count, sum_b / count);

            for (int x = 1; x < w; ++x) {
                // Subtract pixel leaving the window
                const int leave = x - r - 1;
                if (leave >= 0) {
                    int pa, pr, pg, pb;
                    unpack(row[leave], pa, pr, pg, pb);
                    sum_a -= pa; sum_r -= pr; sum_g -= pg; sum_b -= pb;
                    --count;
                }
                // Add pixel entering the window
                const int enter = x + r;
                if (enter < w) {
                    int pa, pr, pg, pb;
                    unpack(row[enter], pa, pr, pg, pb);
                    sum_a += pa; sum_r += pr; sum_g += pg; sum_b += pb;
                    ++count;
                }
                tmp[static_cast<size_t>(y) * w + x] =
                    pack(sum_a / count, sum_r / count, sum_g / count, sum_b / count);
            }
        }

        // Vertical pass (sliding window): base[y,x] = mean of tmp[y-r..y+r, x].
        for (int x = 0; x < w; ++x) {
            int sum_a = 0, sum_r = 0, sum_g = 0, sum_b = 0, count = 0;

            // Initial window: rows [0, min(h-1, r)]
            const int init_bottom = std::min(h - 1, r);
            for (int k = 0; k <= init_bottom; ++k) {
                int pa, pr, pg, pb;
                unpack(tmp[static_cast<size_t>(k) * w + x], pa, pr, pg, pb);
                sum_a += pa; sum_r += pr; sum_g += pg; sum_b += pb;
                ++count;
            }
            base[static_cast<size_t>(0) * stride + x] =
                pack(sum_a / count, sum_r / count, sum_g / count, sum_b / count);

            for (int y = 1; y < h; ++y) {
                // Subtract row leaving the window
                const int leave = y - r - 1;
                if (leave >= 0) {
                    int pa, pr, pg, pb;
                    unpack(tmp[static_cast<size_t>(leave) * w + x], pa, pr, pg, pb);
                    sum_a -= pa; sum_r -= pr; sum_g -= pg; sum_b -= pb;
                    --count;
                }
                // Add row entering the window
                const int enter = y + r;
                if (enter < h) {
                    int pa, pr, pg, pb;
                    unpack(tmp[static_cast<size_t>(enter) * w + x], pa, pr, pg, pb);
                    sum_a += pa; sum_r += pr; sum_g += pg; sum_b += pb;
                    ++count;
                }
                base[static_cast<size_t>(y) * stride + x] =
                    pack(sum_a / count, sum_r / count, sum_g / count, sum_b / count);
            }
        }
    };

#ifdef CHRONON3D_ENABLE_TEXT
    // Hoist the FreeType path builder so the font face is loaded once
    // for the entire batch, not per glyph.
    TextRunPathBuilder ft_builder;
    bool ft_loaded = ft_builder.load(font_path, layout.font_size);
#else
    bool ft_loaded = false;
#endif

    // ── Reusable per-layer renderer ──────────────────────────────────
    // Renders shape.glyphs into `target`.  When `override_color` is set,
    // every glyph is filled (and stroked) with that color — used for the
    // shadow pass.  When `override_color` is nullopt, glyph-level
    // `g.fill` / `g.stroke` are honoured, with `shape.paint.fill` as a
    // default for glyphs whose fill is the "no override" sentinel.
    //
    // `only_tier` (optional): when set, only glyphs whose `g.blur` maps
    // to this tier are drawn.  When nullopt, all glyphs are drawn
    // (existing shadow-pass behavior).
    auto draw_run_layer = [&](
        BLImage& target,
        std::optional<Color> override_color,
        int blur_radius,
        std::optional<int> only_tier = std::nullopt
    ) -> size_t {
        size_t drawn = 0;
        BLContext ctx(target);
        ctx.setCompOp(BL_COMP_OP_SRC_COPY);
        ctx.setFillStyle(BLRgba32(0, 0, 0, 0));
        ctx.fillAll();
        ctx.setCompOp(BL_COMP_OP_SRC_OVER);

        for (size_t gi = 0; gi < shape.glyphs.size(); ++gi) {
            const auto& g = shape.glyphs[gi];

            // ── Per-glyph blur tier filter ──────────────────────────
            // When `only_tier` is set, skip glyphs whose blur value
            // maps to a different tier.  This enables the main-run
            // tiered compositing loop below.
            if (only_tier.has_value()
                && classify_blur_tier(g.blur) != *only_tier) {
                continue;
            }

            // Effective stroke: per-glyph if animator set it, else fall
            // back to shape.paint.stroke when no override is in effect.
            Color eff_stroke = g.stroke;
            f32    eff_stroke_w = g.stroke_width;
            if (!override_color.has_value()
                && g.stroke.a <= 0.0f
                && shape.paint.stroke_enabled) {
                eff_stroke = shape.paint.stroke_color;
                eff_stroke_w = shape.paint.stroke_width;
            }

            const float op_alpha = override_color.has_value()
                ? static_cast<float>(override_color->a) * g.opacity
                : g.opacity;
            if (op_alpha <= 0.0f && eff_stroke.a <= 0.0f) continue;

            BLMatrix2D glyph_mat = build_glyph_matrix(g);
            glyph_mat.translate(-offset_x, -offset_y);

            ctx.save();
            ctx.setTransform(glyph_mat);

#ifdef CHRONON3D_ENABLE_TEXT
            // Stroke first (underneath fill).  Override color wins when
            // supplied; otherwise the effective stroke from above.
            if (eff_stroke.a > 0.0f && eff_stroke_w > 0.0f && ft_loaded) {
                BLPath path = ft_builder.build(
                    layout.placed.glyphs[gi].glyph_id, 0.0f, 0.0f);
                if (!path.empty()) {
                    Color out_stroke = eff_stroke;
                    if (override_color.has_value()) {
                        out_stroke = *override_color;
                        out_stroke.a = eff_stroke.a * g.opacity;
                    }
                    ctx.setStrokeStyle(to_bl_rgba(out_stroke));
                    ctx.setStrokeWidth(static_cast<double>(eff_stroke_w));
                    ctx.strokePath(path);
                }
            }
#endif

            if (op_alpha > 0.0f) {
                ctx.setGlobalAlpha(static_cast<double>(op_alpha));

                Color final_fill = g.fill;
                if (!override_color.has_value()
                    && (g.fill.r >= 0.999f && g.fill.g >= 0.999f
                        && g.fill.b >= 0.999f && g.fill.a >= 0.999f)
                    && (shape.paint.fill.r != 1.0f || shape.paint.fill.g != 1.0f
                        || shape.paint.fill.b != 1.0f || shape.paint.fill.a != 1.0f)) {
                    // "Default white" sentinel: TextPaint fill wins.
                    final_fill = shape.paint.fill;
                }
                if (override_color.has_value()) {
                    final_fill = *override_color;
                }
                ctx.setFillStyle(to_bl_rgba(final_fill));

                auto sgr = SingleGlyphRun::from(layout.placed.glyphs[gi]);
                ctx.fillGlyphRun(BLPoint(0.0, 0.0), font, sgr.bl_run);
            }

            ctx.restore();
            ++drawn;
        }
        ctx.end();

        if (drawn > 0 && blur_radius > 0) {
            apply_box_blur(target, blur_radius);
        }
        return drawn;
    };

    // ── Shadow stack (drawn UNDER the main fill) ─────────────────────
    // Each enabled shadow in `shape.shadows` produces a shadow BLImage
    // with the shadow's color, then composites with the shadow's offset
    // folded into the model matrix.  Multiple shadows compose via
    // over-blending (order = vector order).
    for (const auto& shadow : shape.shadows) {
        if (!shadow.enabled || shadow.opacity <= 0.0f) continue;

        BLImage shadow_img(img_w, img_h, BL_FORMAT_PRGB32);
        const Color shadow_color = {
            shadow.color.r, shadow.color.g, shadow.color.b,
            shadow.color.a * shadow.opacity
        };
        const size_t sh_drawn = draw_run_layer(
            shadow_img, shadow_color, detail::bucket_radius_for_tier(shadow.blur));
        if (sh_drawn == 0) continue;

        // Shadow translation: model + image-local offset + shadow offset.
        Mat4 shadow_model = params.model_matrix;
        shadow_model = glm::translate(
            shadow_model,
            Vec3(offset_x + shadow.offset.x, offset_y + shadow.offset.y, 0.0f));

        chronon3d::blend2d_bridge::composite_bl_image_transformed(
            params.fb, shadow_img, shadow_model,
            params.opacity, BlendMode::Normal);
    }

    // ── Main run: tiered per-glyph blur compositing ──────────────────
    //
    // Instead of drawing all glyphs to one image and blurring with the
    // single max_blur (which smears blur-0 glyphs), we group glyphs into
    // 5 blur tiers, rasterize each tier onto its own surface, apply the
    // tier's box-blur radius, then composite SRC_OVER onto a final image.
    //
    // This means glyphs with blur=0 stay sharp while adjacent blur=16
    // glyphs are fully blurred — the core motivation for per-glyph blur.
    BLImage img(img_w, img_h, BL_FORMAT_PRGB32);
    // Clear the final composite image to transparent.
    {
        BLContext ctx(img);
        ctx.setCompOp(BL_COMP_OP_SRC_COPY);
        ctx.setFillStyle(BLRgba32(0, 0, 0, 0));
        ctx.fillAll();
        ctx.end();
    }

    size_t glyphs_drawn = 0;
    for (int tier = 0; tier < kNumBlurTiers; ++tier) {
        BLImage tier_img(img_w, img_h, BL_FORMAT_PRGB32);
        const size_t drawn = draw_run_layer(
            tier_img, std::nullopt, kBlurTierRadii[tier], tier);
        if (drawn == 0) continue;
        glyphs_drawn += drawn;

        // Composite tier_img onto the final image SRC_OVER.
        {
            BLContext ctx(img);
            ctx.setCompOp(BL_COMP_OP_SRC_OVER);
            ctx.blitImage(BLPoint(0, 0), tier_img);
            ctx.end();
        }
    }

    if (glyphs_drawn == 0) {
        // Nothing visible underneath (e.g. all glyphs had opacity=0 or
        // fell into empty tiers); PR2: explicit outcome with 0 items.
        return graph::RenderOpResult(graph::RenderOpOutcome{0});
    }

    // ── Apply TextMaterial (gradient, bevel, etc.) ───────────────────
    if (shape.material.enabled) {
        apply_text_material(img, shape.material);
    }

    // ── Composite MAIN onto framebuffer — full model_matrix ──────────
    // Compose the user's model with the run-local translate so the
    // image content fills the framebuffer correctly under rotation /
    // scale / shear.  composite_bl_image_transformed has a fast path
    // for simple-translation matrices so the common case is still cheap.
    Mat4 full_model = params.model_matrix;
    full_model = glm::translate(full_model, Vec3(offset_x, offset_y, 0.0f));

    chronon3d::blend2d_bridge::composite_bl_image_transformed(
        params.fb, img, full_model, params.opacity, BlendMode::Normal);

    if (renderer.counters()) {
        renderer.counters()->text_glyphs_rasterized.fetch_add(
            static_cast<uint64_t>(glyphs_drawn),
            std::memory_order_relaxed
        );
    }

    // PR2: no diagnostic_mode here — caller (TextRunNode / multi_source_node) owns diagnostics via ctx.options.diagnostics_enabled.
    return graph::RenderOpResult(graph::RenderOpOutcome{glyphs_drawn});
}

// NOTE: compute_text_run_world_bbox() has been moved to
// src/text/text_run_geometry.cpp (chronon3d_text_core) so the render graph
// can compute bounding boxes without linking the software backend.

// ═══════════════════════════════════════════════════════════════════════════
// create_text_run_processor — ShapeFactory entry
// ═══════════════════════════════════════════════════════════════════════════
//
// The software-registry back-mapping for `ShapeType::TextRun`.  Because
// the canonical renderer path for text runs goes through the
// `TextRunNode` in the render graph (driven by `draw_text_run()` above),
// the registry ShapeProcessor is a thin no-op marker: it carries the
// `is_text_run_shape=true` RenderNode flag and forwards through the
// graph-builder.  This keeps `layer.shape("shape.text_run", ...)` and
// `LayerBuilder::text_run(...)` semantically identical (both flow to a
// TextRunNode downstream).
//
// Why not just call `draw_text_run` directly here?  Because the
// `ShapeProcessor` interface receives a `RenderNode` per node, while
// `draw_text_run` operates on a `SoftwareRenderer` + `TextRunDrawParams`
// pair.  Bridging requires the compositor to invoke the renderer with
// the right z-layer / sample-time context — exactly what the graph
// provides via the TextRunNode's `execute()`.  Keeping the registry
// processor thin avoids duplicating that logic.

#ifdef CHRONON3D_ENABLE_TEXT
std::unique_ptr<ShapeProcessor> create_text_run_processor() {
    struct TextRunProcessor : ShapeProcessor {
        // No-op: the TextRunNode in the render graph handles rasterization.
        // This processor exists only as a registry marker.
        void draw(SoftwareRenderer&, Framebuffer&, const RenderNode&,
                  const RenderState&, const Camera&, i32, i32) override {}

        raster::BBox compute_world_bbox(const Shape&, const Mat4&, f32) override {
            return {0, 0, 0, 0};
        }

        bool hit_test(const Shape&, Vec2, f32) override {
            return false;
        }
    };
    return std::make_unique<TextRunProcessor>();
}
#endif // CHRONON3D_ENABLE_TEXT

} // namespace chronon3d::renderer
