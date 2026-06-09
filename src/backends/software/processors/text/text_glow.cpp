// ---------------------------------------------------------------------------
// text_glow.cpp — Text glow effect implementation
//
// Glow pipeline:
//   1. Extract white alpha mask from raster text into a PADDED buffer (so the
//      blur does not clip at the bounding-box edge — the #1 cause of the
//      “rectangular glow” look).
//   2. Generate three concentric blur passes on the alpha mask only
//      (inner / mid / outer) — using only the alpha channel avoids the muddy
//      grey halo that results from blurring RGB+alpha together.
//   3. Colorise each pass, shifting outer layers toward blue-cyan for a
//      luminous depth effect.
//   4. Accumulate additively and composite with BlendMode::Add.
// ---------------------------------------------------------------------------

#include "text_effects.hpp"
#include "text_processor_helpers.hpp"
#include "../../utils/blend2d_bridge.hpp"
#include <chronon3d/backends/text/text_rasterizer_utils.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <cmath>

namespace chronon3d::renderer {

namespace {

// ── Multi-layer glow descriptor ──────────────────────────────────────
// Each layer defines a blur-radius fraction and an intensity fraction.
// Outer layers get a blue-cyan colour shift for optical depth.
struct GlowLayer {
    float radius_scale;    // fraction of the base radius
    float intensity_scale; // fraction of the base intensity
    float color_shift;     // 0 = glow color, 1 = shifted toward blue-cyan
};

// Default layer configuration — inner tight & bright, mid medium, outer wide & soft.
// Matches the recommended recipe: inner: r*3, a=0.45 | mid: r*10, a=0.22 | outer: r*28, a=0.10
static constexpr std::array<GlowLayer, 3> kGlowLayers{{
    {0.12f, 0.45f, 0.00f},  // inner — tight, colour-accurate
    {0.38f, 0.22f, 0.35f},  // mid   — medium spread, slight blue shift
    {1.00f, 0.10f, 0.65f},  // outer — wide falloff, distinctly blue-cyan
}};

/// Apply a colour shift toward blue-cyan for outer glow layers.
[[nodiscard]] static inline Color shift_glow_color(const Color& base, float shift) {
    if (shift <= 0.0f) return base;
    // Blend toward a cool blue-cyan: {0.20, 0.55, 1.0}
    const Color cool_blue{0.20f, 0.55f, 1.0f, 1.0f};
    return Color{
        base.r + (cool_blue.r - base.r) * shift,
        base.g + (cool_blue.g - base.g) * shift,
        base.b + (cool_blue.b - base.b) * shift,
        base.a
    };
}

/// Build the padded white alpha mask from the rasterised text.
/// Returns (mask_image, padding) — the mask is larger than the raster by
/// 2×padding on each axis.
struct PaddedMask {
    BLImage image;
    int padding;
};

[[nodiscard]] static inline PaddedMask make_padded_alpha_mask(
    const BLImage& raster_img,
    int base_padding
) {
    const int rw = raster_img.width();
    const int rh = raster_img.height();
    const int pad = std::max(base_padding, 8);  // never < 8 px
    const int pw = rw + 2 * pad;
    const int ph = rh + 2 * pad;

    BLImage mask;
    mask.create(pw, ph, BL_FORMAT_PRGB32);

    BLContext ctx(mask);
    // Clear to transparent
    ctx.setCompOp(BL_COMP_OP_SRC_COPY);
    ctx.setFillStyle(BLRgba32(0, 0, 0, 0));
    ctx.fillAll();

    // Copy the raster into the centre of the padded buffer
    ctx.blitImage(BLPoint(pad, pad), raster_img);

    // Convert to white-on-transparent via SRC_IN with white.
    // This extracts only the alpha channel of the raster and sets RGB = (1,1,1),
    // so the blur operates on a pure-alpha signal with no colour contamination.
    ctx.setCompOp(BL_COMP_OP_SRC_IN);
    ctx.setFillStyle(BLRgba32(255, 255, 255, 255));
    ctx.fillAll();
    ctx.end();

    return {std::move(mask), pad};
}

/// Create a blurred glow layer, colourised and scaled by intensity.
/// The result is written into `accum` with additive compositing (PLUS).
///
/// Pipeline per layer:
///   1. Copy white alpha mask → layer_img
///   2. Blur layer_img (operates on premultiplied white = (a, a, a, a))
///   3. SRC_IN with (layer_color.rgb, layer_intensity)
///      → result = layer_color * blurred_alpha, result.a = layer_intensity * blurred_alpha
///      This cleanly colourises the glow without muddy RGB-from-alpha contamination.
///   4. PLUS onto accum
static inline void build_and_accumulate_layer(
    BLImage& accum,
    const BLImage& alpha_mask,
    const GlowLayer& layer_desc,
    const Color& base_glow_color,
    float base_radius,
    float base_intensity
) {
    const float layer_radius = std::max(1.5f, base_radius * layer_desc.radius_scale);
    const float layer_intensity = base_intensity * layer_desc.intensity_scale;
    if (layer_intensity <= 0.0f) return;

    // 1. Copy alpha mask into working buffer
    const int w = alpha_mask.width();
    const int h = alpha_mask.height();
    BLImage layer_img;
    layer_img.create(w, h, BL_FORMAT_PRGB32);
    {
        BLContext ctx(layer_img);
        ctx.setCompOp(BL_COMP_OP_SRC_COPY);
        ctx.blitImage(BLPoint(0, 0), alpha_mask);
        ctx.end();
    }

    // 2. Blur — operates on all PRGB32 channels, but because the mask is
    //    white-on-transparent, R=G=B=A holds true, so the blur effectively
    //    diffuses the alpha shape without colour bleeding.
    blur_bl_image_inplace(layer_img, layer_radius);

    // 3. Colorise and scale intensity via SRC_IN.
    //    BL_COMP_OP_SRC_IN with fill (R,G,B,A):
    //      result.rgb = fill.rgb * dest.a   (pixel alpha of the blurred mask)
    //      result.a   = fill.a   * dest.a
    //    Since blurred mask has R=G=B=A (premultiplied white), this cleanly
    //    applies the glow colour and intensity to the alpha shape.
    const Color layer_color = shift_glow_color(base_glow_color, layer_desc.color_shift);
    const uint8_t intensity_byte = static_cast<uint8_t>(
        std::clamp(layer_intensity * 255.0f, 0.0f, 255.0f)
    );
    {
        BLContext ctx(layer_img);
        ctx.setCompOp(BL_COMP_OP_SRC_IN);
        ctx.setFillStyle(BLRgba32(
            static_cast<uint8_t>(std::clamp(layer_color.r * 255.0f, 0.0f, 255.0f)),
            static_cast<uint8_t>(std::clamp(layer_color.g * 255.0f, 0.0f, 255.0f)),
            static_cast<uint8_t>(std::clamp(layer_color.b * 255.0f, 0.0f, 255.0f)),
            intensity_byte
        ));
        ctx.fillAll();
        ctx.end();
    }

    // 4. Additive accumulate onto accum
    {
        BLContext ctx(accum);
        ctx.setCompOp(BL_COMP_OP_PLUS);
        ctx.blitImage(BLPoint(0, 0), layer_img);
        ctx.end();
    }
}

} // anonymous namespace

// ── Public API ────────────────────────────────────────────────────────

void draw_text_glow(SoftwareRenderer& renderer, Framebuffer& fb, const RenderNode& node,
                    const RenderState& state, const TextRasterization& raster,
                    float effective_size) {
    CHRONON_ZONE_C("text_glow", trace_category::kText);
    const Mat4& model = state.matrix;
    const f32 opacity = state.opacity;

    const bool use_geo_transform = !state.projection.ready &&
                                   is_affine_transform(model) &&
                                   has_non_translation(model);

    const CacheKey key = hash_glow_params(node, effective_size);
    std::shared_ptr<BLImage> glow_cache;
    {
        std::lock_guard<std::mutex> lock(text_glow_cache_mutex());
        auto cached = get_glow_cache().get(key);
        if (cached) {
            glow_cache = *cached;
            if (profiling::g_current_counters) {
                profiling::g_current_counters->text_glow_cache_hits.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }

    if (!glow_cache) {
        if (profiling::g_current_counters) {
            profiling::g_current_counters->text_glow_cache_misses.fetch_add(1, std::memory_order_relaxed);
        }

        const f32 base_radius = std::max(1.0f, node.glow.radius);

        // Padding must be large enough for the widest blur pass (outer = radius * 1.0).
        // Rule of thumb: padding = ceil(radius * 4) to avoid edge clipping.
        const int padding = static_cast<int>(std::ceil(base_radius * 4.0f)) + 4;

        // ── Step 1: Build padded white alpha mask ────────────────────
        auto [alpha_mask, actual_pad] = make_padded_alpha_mask(raster.image, padding);

        // ── Step 2: Accumulate multiple glow layers ──────────────────
        BLImage glow_accum;
        glow_accum.create(alpha_mask.width(), alpha_mask.height(), BL_FORMAT_PRGB32);
        {
            BLContext ctx(glow_accum);
            ctx.setCompOp(BL_COMP_OP_SRC_COPY);
            ctx.setFillStyle(BLRgba32(0, 0, 0, 0));
            ctx.fillAll();
            ctx.end();
        }

        const Color& glow_color = node.glow.color;
        const float glow_intensity = node.glow.intensity;

        for (const auto& layer_desc : kGlowLayers) {
            build_and_accumulate_layer(glow_accum, alpha_mask, layer_desc,
                                       glow_color, base_radius, glow_intensity);
        }

        auto cached_img = std::make_shared<BLImage>(glow_accum);
        {
            std::lock_guard<std::mutex> lock(text_glow_cache_mutex());
            size_t weight = cached_img->width() * cached_img->height() * 4;
            get_glow_cache().put(key, cached_img, weight);
        }
        glow_cache = cached_img;
    }

    // ── Composite with additive blend ────────────────────────────────
    // The glow buffer is padded, so we shift the compositing position
    // by -padding.  The padding is derived from the base radius;
    // recover it from the difference between the cached image and the
    // raster (or re-derive it from the node params).
    const f32 base_radius = std::max(1.0f, node.glow.radius);
    const int padding = static_cast<int>(std::ceil(base_radius * 4.0f)) + 4;

    if (glow_cache) {
        // NOTE: node.glow.intensity is ALREADY baked into each glow layer via
        // SRC_IN during build_and_accumulate_layer (where layer_intensity =
        // base_intensity * layer_desc.intensity_scale).  Do NOT multiply by
        // it again here — that would square the intensity and make the glow
        // ~30% dimmer than intended.
        const f32 glow_alpha = opacity * node.glow.color.a;

        if (use_geo_transform) {
            int x = static_cast<int>(std::lround(raster.x_offset)) - padding;
            int y = static_cast<int>(std::lround(raster.y_offset)) - padding;
            chronon3d::blend2d_bridge::composite_bl_image(fb, *glow_cache, x, y,
                                                          glow_alpha, BlendMode::Add);
        } else {
            Mat4 glow_model = model * glm::translate(Mat4(1.0f),
                Vec3(raster.x_offset - static_cast<f32>(padding),
                     raster.y_offset - static_cast<f32>(padding), 0.0f));
            chronon3d::blend2d_bridge::composite_bl_image_transformed(
                fb, *glow_cache, glow_model, glow_alpha, BlendMode::Add);
        }
    }
}

} // namespace chronon3d::renderer
