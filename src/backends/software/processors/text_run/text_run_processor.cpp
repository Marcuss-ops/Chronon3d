#include <chronon3d/backends/software/text_run_processor.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
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

#ifdef CHRONON3D_ENABLE_TEXT
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_OUTLINE_H
#endif

namespace chronon3d::renderer {

namespace {

// ── Helper: Blend2D color conversion (inline, local to this TU) ───────

static inline BLRgba32 to_bl_rgba(const Color& c) {
    return BLRgba32(
        static_cast<uint8_t>(std::clamp(c.r * 255.0f, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp(c.g * 255.0f, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp(c.b * 255.0f, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp(c.a * 255.0f, 0.0f, 255.0f))
    );
}

// ── Blend2D resource cache (shared across text runs) ──────────────────
// NOTE: This duplicates blend2d_resources() from text_rasterizer_render.cpp
// because that cache is in an anonymous namespace there.  Future refactor
// could extract it to a shared header.

struct TextRunBlResources {
    std::unordered_map<std::string, BLFontFace> faces;
    std::mutex mutex;

    BLFontFace get_face(const std::string& path) {
        std::lock_guard<std::mutex> lock(mutex);
        const std::string resolved = AssetRegistry::resolve(path);
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
    const double ax = static_cast<double>(g.anchor.x);
    const double ay = static_cast<double>(g.anchor.y);
    const double sz_x = static_cast<double>(g.scale.x);
    const double sz_y = static_cast<double>(g.scale.y);

    // Rotation Z in radians (degrees → radians)
    const double angle = static_cast<double>(g.rotation.z) * (kPi / 180.0);
    const double cos_a = std::cos(angle);
    const double sin_a = std::sin(angle);

    // Skew: tan(skew_angle) applied along skew_axis direction
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

    // 2. Translate by animated position
    m.translate(px, py);

    // 3. Translate to anchor
    m.translate(ax, ay);

    // 4. Scale, then skew along axis, then rotate.
    BLMatrix2D srt;
    srt.reset(sz_x, 0.0, 0.0, sz_y, 0.0, 0.0);   // start with scale

    // Skew along axis: R(+axis) × SkewX × R(-axis) × Scale
    if (std::abs(skew_rad) > 1e-9) {
        BLMatrix2D rot_to_axis;
        rot_to_axis.reset(cos_axis, sin_axis, -sin_axis, cos_axis, 0.0, 0.0);

        BLMatrix2D rot_from_axis;
        rot_from_axis.reset(cos_axis, -sin_axis, sin_axis, cos_axis, 0.0, 0.0);

        BLMatrix2D skew_x;
        skew_x.reset(1.0, 0.0, tan_skew, 1.0, 0.0, 0.0);

        // Compose: R(+axis) × SkewX × R(-axis) × Scale
        BLMatrix2D skew_composed = rot_to_axis;
        skew_composed.transform(skew_x);
        skew_composed.transform(rot_from_axis);
        srt = skew_composed;
        BLMatrix2D scale_mat;
        scale_mat.reset(sz_x, 0.0, 0.0, sz_y, 0.0, 0.0);
        srt.transform(scale_mat);
    }

    // Rotate: R × SkewScale
    BLMatrix2D rot;
    rot.reset(cos_a, sin_a, -sin_a, cos_a, 0.0, 0.0);
    BLMatrix2D composed = rot;
    composed.transform(srt);
    m.transform(composed);

    // 5. Translate back from anchor
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
        const std::string resolved = AssetRegistry::resolve(font_path);
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

bool draw_text_run(
    SoftwareRenderer& renderer,
    TextRunDrawParams& params
) {
    const auto& shape = params.shape;
    if (!shape.layout || shape.glyphs.empty()) return false;

    const auto& layout = *shape.layout;
    const std::string& font_path = layout.font.font_path;
    if (font_path.empty()) return false;

    CHRONON_ZONE_C("text_run_draw", trace_category::kText);
    const auto draw_start = params.diagnostic_mode
        ? profiling::now() : profiling::Clock::time_point{};

    // Load font face
    BLFontFace face = text_run_bl_resources().get_face(font_path);
    if (face.empty()) return false;

    BLFont font;
    font.createFromFace(face, layout.font_size);

    // ── Compute bounding box of all glyphs ───────────────────────────
    // NOTE: This is a conservative estimate using only layout + position offsets.
    // Per-glyph rotation, scale, skew, and anchor are NOT accounted for,
    // so extreme transforms may cause clipping.  The padding (8px + margin)
    // should absorb moderate transforms.
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
    }

    const float margin = 8.0f;
    const int img_w = std::max(1, static_cast<int>(std::ceil(max_x - min_x + margin * 2.0f)));
    const int img_h = std::max(1, static_cast<int>(std::ceil(max_y - min_y + margin * 2.0f)));
    const float offset_x = min_x - margin;
    const float offset_y = min_y - margin;

    // Create intermediate image
    BLImage img(img_w, img_h, BL_FORMAT_PRGB32);
    BLContext ctx(img);
    ctx.setCompOp(BL_COMP_OP_SRC_COPY);
    ctx.setFillStyle(BLRgba32(0, 0, 0, 0));
    ctx.fillAll();
    ctx.setCompOp(BL_COMP_OP_SRC_OVER);

    // ── Draw each glyph: first stroke (behind), then fill ───────────
    size_t glyphs_drawn = 0;

#ifdef CHRONON3D_ENABLE_TEXT
    // Hoist the FreeType path builder so the font face is loaded once
    // for the entire batch, not per glyph.
    TextRunPathBuilder ft_builder;
    bool ft_loaded = ft_builder.load(font_path, layout.font_size);
#endif

    for (size_t gi = 0; gi < shape.glyphs.size(); ++gi) {
        const auto& g = shape.glyphs[gi];
        if (g.opacity <= 0.0f && g.stroke.a <= 0.0f) continue;

        // Build per-glyph matrix
        BLMatrix2D glyph_mat = build_glyph_matrix(g);
        // Offset to image-local coordinates
        glyph_mat.translate(-offset_x, -offset_y);

        ctx.save();
        ctx.setTransform(glyph_mat);

        // Stroke (behind fill, drawn first)
#ifdef CHRONON3D_ENABLE_TEXT
        if (g.stroke.a > 0.0f && g.stroke_width > 0.0f && ft_loaded) {
            BLPath path = ft_builder.build(layout.placed.glyphs[gi].glyph_id, 0.0f, 0.0f);
            if (!path.empty()) {
                ctx.setStrokeStyle(to_bl_rgba(g.stroke));
                ctx.setStrokeWidth(static_cast<double>(g.stroke_width));
                ctx.strokePath(path);
            }
        }
#endif

        // Fill
        if (g.opacity > 0.0f) {
            ctx.setGlobalAlpha(static_cast<double>(g.opacity));
            ctx.setFillStyle(to_bl_rgba(g.fill));

            auto sgr = SingleGlyphRun::from(layout.placed.glyphs[gi]);
            ctx.fillGlyphRun(BLPoint(0.0, 0.0), font, sgr.bl_run);
        }

        ctx.restore();
        ++glyphs_drawn;
    }

    ctx.end();

    if (glyphs_drawn == 0) return false;

    // ── Apply TextMaterial (gradient, bevel, etc.) ───────────────────
    if (shape.material.enabled) {
        apply_text_material(img, shape.material);
    }

    // ── Composite onto framebuffer ───────────────────────────────────
    const Mat4& model = params.model_matrix;
    const float opacity = params.opacity;

    const int cx = static_cast<int>(std::lround(model[3][0] + offset_x));
    const int cy = static_cast<int>(std::lround(model[3][1] + offset_y));

    chronon3d::blend2d_bridge::composite_bl_image(params.fb, img, cx, cy, opacity, BlendMode::Normal);

    if (renderer.counters()) {
        renderer.counters()->text_glyphs_rasterized.fetch_add(
            static_cast<uint64_t>(glyphs_drawn),
            std::memory_order_relaxed
        );
    }

    if (params.diagnostic_mode) {
        const double total_ms = profiling::elapsed_ms(draw_start);
        spdlog::info(
            "[TextRun] glyphs={} img={}x{} total={:.2f}ms",
            glyphs_drawn, img_w, img_h, total_ms
        );
    }

    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// compute_text_run_world_bbox
// ═══════════════════════════════════════════════════════════════════════════
//
// NOTE: This is a conservative approximation.  It uses layout positions +
// position offsets only, and does NOT account for per-glyph rotation, scale,
// or skew transforms.  For accurate bbox after arbitrary per-glyph transforms,
// compute the bbox from the rasterized image instead.

raster::BBox compute_text_run_world_bbox(
    const TextRunShape& shape,
    const Mat4& model,
    f32 spread
) {
    if (!shape.layout || shape.glyphs.empty()) {
        return {0, 0, 0, 0};
    }

    float min_x = 1e10f, max_x = -1e10f;
    float min_y = 1e10f, max_y = -1e10f;

    for (const auto& g : shape.glyphs) {
        const float gx = g.layout_position.x + g.position.x;
        const float gy = g.layout_position.y + g.position.y;
        const float pad = g.blur + g.stroke_width + 8.0f + spread;
        min_x = std::min(min_x, gx - pad);
        max_x = std::max(max_x, gx + pad + 12.0f);  // approximate glyph advance
        min_y = std::min(min_y, gy - pad);
        max_y = std::max(max_y, gy + pad + shape.layout->placed.total_height);
    }

    // Transform corners to world space
    Vec4 corners[4] = {
        model * Vec4(min_x, min_y, 0.0f, 1.0f),
        model * Vec4(max_x, min_y, 0.0f, 1.0f),
        model * Vec4(max_x, max_y, 0.0f, 1.0f),
        model * Vec4(min_x, max_y, 0.0f, 1.0f)
    };

    f32 wx_min = 1e10f, wx_max = -1e10f;
    f32 wy_min = 1e10f, wy_max = -1e10f;
    for (const auto& c : corners) {
        if (std::abs(c.w) > 1e-7f) {
            wx_min = std::min(wx_min, c.x / c.w);
            wx_max = std::max(wx_max, c.x / c.w);
            wy_min = std::min(wy_min, c.y / c.w);
            wy_max = std::max(wy_max, c.y / c.w);
        } else {
            wx_min = std::min(wx_min, c.x);
            wx_max = std::max(wx_max, c.x);
            wy_min = std::min(wy_min, c.y);
            wy_max = std::max(wy_max, c.y);
        }
    }

    const f32 pad = spread + 20.0f;
    return {
        static_cast<i32>(std::floor(wx_min - pad)),
        static_cast<i32>(std::floor(wy_min - pad)),
        static_cast<i32>(std::ceil(wx_max + pad)),
        static_cast<i32>(std::ceil(wy_max + pad))
    };
}

} // namespace chronon3d::renderer
