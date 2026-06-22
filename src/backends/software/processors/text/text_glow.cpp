// ---------------------------------------------------------------------------
// text_glow.cpp — Text glow effect implementation
//
// Unified glow pipeline (Item 17)
// -------------------------------
// Text glow now delegates to the shared `GlowPipeline::render()` entry point
// (effects/glow_pipeline.hpp) instead of running its own BLImage-based
// blur/colourise/accumulate loop.  The text-specific step — building a
// padded white alpha mask from the rasterised glyphs — remains here.
//
// Pipeline:
//   1. Build padded white alpha mask from text raster (BLImage)
//   2. Convert alpha mask to a Framebuffer
//   3. Call GlowPipeline::render() with source_is_alpha_mask = true
//   4. Convert result back to BLImage for compositing (preserving the
//      Blend2D-based composite path with affine transform support)
//   5. Cache the result as BLImage (same cache as before)
// ---------------------------------------------------------------------------

#include "text_effects.hpp"
#include "text_processor_helpers.hpp"
#include "../../utils/blend2d_bridge.hpp"
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/backends/text/text_rasterizer_utils.hpp>
#include <chronon3d/core/config.hpp>   // for DebugConfig type (used by make_padded_alpha_mask)
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/effects/glow_pipeline.hpp>
#include <chronon3d/render_graph/render_graph_context.hpp>
#include <chronon3d/simd/kernels.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <cmath>
// R2: function now consumes the slim processor context POD.
#include <chronon3d/backends/software/software_processor_context.hpp>

namespace chronon3d::renderer {

namespace {

/// Build the padded white alpha mask from the rasterised text.
/// Returns (mask_image, padding) — the mask is larger than the raster by
/// 2×padding on each axis.
struct PaddedMask {
    BLImage image;
    int padding;
};

[[nodiscard]] static inline PaddedMask make_padded_alpha_mask(
    const BLImage& raster_img,
    int base_padding,
    const DebugConfig& debug
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
    // This extracts only the alpha channel of the raster and sets RGB = (1,1,1).
    ctx.setCompOp(BL_COMP_OP_SRC_IN);
    ctx.setFillStyle(BLRgba32(255, 255, 255, 255));
    ctx.fillAll();
    ctx.end();

    // ── Alpha threshold ──────────────────────────────────────────────
    // Eliminate sub-pixel alpha noise (< 16/255) around glyph bounding boxes
    // which the blur would amplify into a rectangular fog.
    {
        BLImageData md;
        if (mask.getData(&md) == BL_SUCCESS && md.pixelData) {
            const int stride = static_cast<int>(md.stride / sizeof(uint32_t));
            auto* px = static_cast<uint32_t*>(md.pixelData);
            for (int y = 0; y < ph; ++y) {
                for (int x = 0; x < pw; ++x) {
                    const uint32_t a = px[y * stride + x] >> 24;
                    if (a > 0 && a < 16) {
                        px[y * stride + x] = 0;
                    }
                }
            }
        }
    }

    // ── Debug save ───────────────────────────────────────────────────
    if (debug.dump_alpha_mask()) {
        BLImageData md;
        if (mask.getData(&md) == BL_SUCCESS && md.pixelData) {
            const int stride = static_cast<int>(md.stride / sizeof(uint32_t));
            Framebuffer debug_fb(pw, ph);
            const auto* px = static_cast<const uint32_t*>(md.pixelData);
            for (int y = 0; y < ph; ++y) {
                for (int x = 0; x < pw; ++x) {
                    const uint32_t p = px[y * stride + x];
                    debug_fb.set_pixel(x, y, Color{
                        static_cast<float>((p >> 16) & 0xFF) / 255.0f,
                        static_cast<float>((p >>  8) & 0xFF) / 255.0f,
                        static_cast<float>( p        & 0xFF) / 255.0f,
                        static_cast<float>((p >> 24) & 0xFF) / 255.0f
                    });
                }
            }
            save_png(debug_fb, "output/debug_alpha_mask.png");
        }
    }

    return {std::move(mask), pad};
}

/// Convert a BLImage (PRGB32 format) to a Framebuffer.
/// PRGB32 stores premultiplied RGBA, so we un-premultiply on read.
/// Uses SIMD-accelerated row conversion (chronon3d::simd::bl_image_prgb32_to_color_row).
[[nodiscard]] static inline std::shared_ptr<Framebuffer> bl_image_to_framebuffer(const BLImage& img) {
    const int w = img.width();
    const int h = img.height();
    auto fb = std::make_shared<Framebuffer>(w, h);
    BLImageData md;
    if (img.getData(&md) == BL_SUCCESS && md.pixelData) {
        const int stride = static_cast<int>(md.stride / sizeof(uint32_t));
        const auto* px = static_cast<const uint32_t*>(md.pixelData);
        for (int y = 0; y < h; ++y) {
            chronon3d::simd::bl_image_prgb32_to_color_row(
                fb->pixels_row(y), px + y * stride, w);
        }
    }
    return fb;
}

/// Convert a Framebuffer to a BLImage (PRGB32 format).
/// Uses SIMD-accelerated row conversion (chronon3d::simd::color_to_prgb32_row).
[[nodiscard]] static inline BLImage framebuffer_to_bl_image(const Framebuffer& fb) {
    const int w = fb.width();
    const int h = fb.height();
    BLImage img;
    img.create(w, h, BL_FORMAT_PRGB32);
    BLImageData md;
    if (img.getData(&md) == BL_SUCCESS && md.pixelData) {
        const int stride = static_cast<int>(md.stride / sizeof(uint32_t));
        auto* px = static_cast<uint32_t*>(md.pixelData);
        for (int y = 0; y < h; ++y) {
            chronon3d::simd::color_to_prgb32_row(
                px + y * stride, fb.pixels_row(y), w);
        }
    }
    return img;
}

} // anonymous namespace

// ── Public API ────────────────────────────────────────────────────────

void draw_text_glow(const SoftwareProcessorContext& rctx, Framebuffer& fb, const RenderNode& node,
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
                profiling::g_current_counters->glow_cache_hits.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }

    if (!glow_cache) {
        if (profiling::g_current_counters) {
            profiling::g_current_counters->text_glow_cache_misses.fetch_add(1, std::memory_order_relaxed);
            profiling::g_current_counters->glow_cache_misses.fetch_add(1, std::memory_order_relaxed);
        }

        const f32 base_radius = std::max(1.0f, node.glow.radius);
        const int padding = static_cast<int>(std::ceil(base_radius * 4.0f)) + 8;

        // ── Step 1: Build padded white alpha mask (text-specific) ────
        const DebugConfig& debug_ref = rctx.debug_config ? *rctx.debug_config : DebugConfig{};
        auto [alpha_mask, actual_pad] = make_padded_alpha_mask(raster.image, padding, debug_ref);

        // ── Step 2: Run the unified GlowPipeline ─────────────────────
        // Convert alpha mask to Framebuffer and feed it to the shared
        // pipeline as source_is_alpha_mask so the trigger is taken
        // directly from source alpha.
        auto mask_fb = bl_image_to_framebuffer(alpha_mask);

        GlowPipelineInput input;
        input.source = mask_fb.get();
        input.source_is_alpha_mask = true;
        // Copy all glow params directly from the unified GlowParams struct
        input.params = node.glow;
        input.params.radius = base_radius;

        // Minimal context — the counters/pool are optional.
        graph::RenderGraphContext ctx{};
        if (profiling::g_current_counters) {
            ctx.node_exec.counters = profiling::g_current_counters;
        }
        // TICKET-007 — thread per-instance DebugConfig (from owning
        // SoftwareRenderer) into the locally-built context so that
        // GlowPipeline::render reads the correct flag and the per-pass
        // debug artefacts honour the engine's debug.glow() flag.
        ctx.policy.debug_config = rctx.debug_config;

        auto output = GlowPipeline::render(ctx, input);

        // ── Step 3: Convert result to BLImage and cache ──────────────
        BLImage glow_result = framebuffer_to_bl_image(*output.composited);
        auto cached_img = std::make_shared<BLImage>(std::move(glow_result));
        {
            std::lock_guard<std::mutex> lock(text_glow_cache_mutex());
            size_t weight = cached_img->width() * cached_img->height() * 4;
            get_glow_cache().put(key, cached_img, weight);
        }
        glow_cache = cached_img;
    }

    // ── Composite with additive blend ────────────────────────────────
    const f32 base_radius = std::max(1.0f, node.glow.radius);
    const int padding = static_cast<int>(std::ceil(base_radius * 4.0f)) + 8;

    if (glow_cache) {
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
