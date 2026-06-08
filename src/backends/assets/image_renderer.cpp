#include <chronon3d/backends/assets/image_renderer.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/scene/model/mask_utils.hpp>
#include <chronon3d/media/media_placement.hpp>
#include <chronon3d/core/telemetry/render_telemetry.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include "../software/utils/blend2d_bridge.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <sstream>

namespace chronon3d {

namespace {

float rounded_rect_coverage(float x, float y, float w, float h, float radius) {
    if (radius <= 0.0f) {
        return 1.0f;
    }

    const float r = std::min({radius, w * 0.5f, h * 0.5f});
    const float inner_x0 = r;
    const float inner_y0 = r;
    const float inner_x1 = w - r;
    const float inner_y1 = h - r;
    const float cx = std::clamp(x, inner_x0, inner_x1);
    const float cy = std::clamp(y, inner_y0, inner_y1);
    const float dx = x - cx;
    const float dy = y - cy;
    const float dist = std::sqrt(dx * dx + dy * dy);

    return std::clamp(r + 0.5f - dist, 0.0f, 1.0f);
}

std::string rounded_cache_key(const std::string& path, int width, int height, float radius) {
    const int quantized_radius = static_cast<int>(std::round(radius * 64.0f));
    std::ostringstream key;
    key << path << '#' << width << 'x' << height << "#r" << quantized_radius;
    return key.str();
}

void record_image_telemetry(
    const RenderState& state,
    const std::string& image_path,
    int image_width,
    int image_height,
    const char* cache_status,
    double decode_ms,
    double sample_ms,
    uint64_t sampled_pixels
) {
    telemetry::ImageTelemetryRecord rec;
    rec.frame_number = state.frame_number;
    rec.layer_id = state.layer_id;
    rec.image_path = image_path;
    rec.image_width = image_width;
    rec.image_height = image_height;
    rec.cache_status = cache_status ? cache_status : "";
    rec.decode_ms = decode_ms;
    rec.sample_ms = sample_ms;
    rec.sampled_pixels = sampled_pixels;
    telemetry::record_image_telemetry(std::move(rec));
}

} // namespace

std::shared_ptr<const Framebuffer> ImageRenderer::rounded_framebuffer(
    const std::string& path,
    const CachedImage& cached,
    float radius
) {
    if (!cached.fb_img || radius <= 0.0f) {
        return nullptr;
    }

    const std::string key = rounded_cache_key(path, cached.width, cached.height, radius);
    {
        std::lock_guard<std::mutex> lock(*m_rounded_mutex);
        auto it = m_rounded_framebuffers.find(key);
        if (it != m_rounded_framebuffers.end()) {
            return it->second;
        }
    }

    auto rounded = std::make_shared<Framebuffer>(cached.width, cached.height);
    rounded->set_opaque(false);

    const float w = static_cast<float>(cached.width);
    const float h = static_cast<float>(cached.height);
    for (int y = 0; y < cached.height; ++y) {
        const Color* src_row = cached.fb_img->pixels_row(y);
        Color* dst_row = rounded->pixels_row(y);
        for (int x = 0; x < cached.width; ++x) {
            const float coverage = rounded_rect_coverage(
                static_cast<float>(x) + 0.5f,
                static_cast<float>(y) + 0.5f,
                w,
                h,
                radius
            );
            Color c = src_row[x];
            c.r *= coverage;
            c.g *= coverage;
            c.b *= coverage;
            c.a *= coverage;
            dst_row[x] = c;
        }
    }

    {
        std::lock_guard<std::mutex> lock(*m_rounded_mutex);
        m_rounded_framebuffers[key] = rounded;
    }
    return rounded;
}

bool ImageRenderer::draw_image(const ImageShape& image, const RenderState& state, Framebuffer& fb) {
    if (image.path.empty() || image.size.x <= 0 || image.size.y <= 0) {
        return false;
    }

    const CachedImage* cached = nullptr;
    const auto t_decode0 = std::chrono::steady_clock::now();
    cached = ImageCache::instance().get_or_load(image.path);
    const auto t_decode1 = std::chrono::steady_clock::now();
    const double decode_ms = std::chrono::duration<double, std::milli>(t_decode1 - t_decode0).count();
    if (profiling::g_current_counters && decode_ms > 0.0) {
        profiling::g_current_counters->image_decode_ms.fetch_add(
            static_cast<uint64_t>(std::llround(decode_ms)),
            std::memory_order_relaxed
        );
    }

    BLImage render_img;
    int img_w = 0;
    int img_h = 0;
    bool using_placeholder = false;

    if (!cached || cached->bl_img.empty()) {
        int pw = static_cast<int>(std::round(image.size.x));
        int ph = static_cast<int>(std::round(image.size.y));
        pw = std::max(1, pw);
        ph = std::max(1, ph);

        render_img = BLImage(pw, ph, BL_FORMAT_PRGB32);
        BLContext ctx(render_img);
        ctx.setFillStyle(BLRgba32(0x3A, 0x0C, 0x0C, 0xFF)); // Deep red bg
        ctx.fillRect(0, 0, pw, ph);
        ctx.setStrokeStyle(BLRgba32(0xFF, 0x44, 0x44, 0xFF)); // Crimson border and cross
        ctx.setStrokeWidth(2.0f);
        ctx.strokeRect(0, 0, pw, ph);
        ctx.strokeLine(0, 0, pw, ph);
        ctx.strokeLine(0, ph, pw, 0);
        ctx.end();

        img_w = pw;
        img_h = ph;
        using_placeholder = true;
    } else {
        render_img = cached->bl_img;
        img_w = cached->width;
        img_h = cached->height;
    }

    const f32 final_opacity = image.opacity * state.opacity;
    if (final_opacity <= 0.001f) return true;

    // Handle ImageCrop (if enabled, crop origin and size are normalized relative to cached image)
    Vec2 original_source_size = Vec2(static_cast<float>(img_w), static_cast<float>(img_h));
    Vec2 effective_source_size = original_source_size;
    Vec2 effective_source_origin = Vec2(0.0f, 0.0f);
    
    // Skip crop on placeholders to avoid clipping fallback logic
    if (image.crop.enabled && !using_placeholder) {
        effective_source_size = image.crop.size * original_source_size;
        effective_source_origin = image.crop.origin * original_source_size;
    }

    // Call compute_media_placement
    // Force placeholders to use Stretch fit to match requested size exactly
    FitMode resolved_fit = using_placeholder ? FitMode::Stretch : image.fit;
    MediaPlacementResult placement = compute_media_placement(
        effective_source_size,
        image.size,
        resolved_fit,
        image.focal_point
    );

    Vec2 final_src_origin = effective_source_origin + placement.src_rect.origin;
    Vec2 final_src_size = placement.src_rect.size;

    int src_x = static_cast<int>(std::round(final_src_origin.x));
    int src_y = static_cast<int>(std::round(final_src_origin.y));
    int src_w = static_cast<int>(std::round(final_src_size.x));
    int src_h = static_cast<int>(std::round(final_src_size.y));

    // Clamp values to stay within bounds
    src_x = std::clamp(src_x, 0, img_w);
    src_y = std::clamp(src_y, 0, img_h);
    src_w = std::clamp(src_w, 0, img_w - src_x);
    src_h = std::clamp(src_h, 0, img_h - src_y);

    if (src_w <= 0 || src_h <= 0) return true;

    // Scale mapping from sub_img space [0,0 -> src_w,src_h] to destination dst_rect
    Vec2 scale = placement.dst_rect.size / Vec2(static_cast<float>(src_w), static_cast<float>(src_h));
    Mat4 scaled_model = state.matrix
                      * glm::translate(Mat4(1.0f), Vec3(placement.dst_rect.origin, 0.0f))
                      * glm::scale(Mat4(1.0f), Vec3(scale, 1.0f));

    // Scale the radius from destination space to sub-image source space.
    float scaled_radius = 0.0f;
    if (image.radius > 0.0f && placement.dst_rect.size.x > 0.0f) {
        scaled_radius = image.radius * (static_cast<float>(src_w) / placement.dst_rect.size.x);
    }

    const bool mask_enabled = state.mask && state.mask->enabled();
    const int clip_x0 = state.clip_rect ? state.clip_rect->x0 : -1;
    const int clip_y0 = state.clip_rect ? state.clip_rect->y0 : -1;
    const int clip_x1 = state.clip_rect ? state.clip_rect->x1 : -1;
    const int clip_y1 = state.clip_rect ? state.clip_rect->y1 : -1;
    spdlog::debug(
        "[image-render] layer='{}' path='{}' cached={}x{} sub={}x{} requested={}x{} scale=({:.4f},{:.4f}) opacity={:.3f} mask={} clip={} clip_rect=[{},{} -> {},{}] tx={:.2f} ty={:.2f}",
        state.layer_id,
        image.path,
        img_w,
        img_h,
        src_w,
        src_h,
        image.size.x,
        image.size.y,
        scale.x,
        scale.y,
        final_opacity,
        mask_enabled ? 1 : 0,
        state.clip_rect ? 1 : 0,
        clip_x0,
        clip_y0,
        clip_x1,
        clip_y1,
        scaled_model[3][0],
        scaled_model[3][1]
    );

    const bool full_source = src_x == 0 && src_y == 0 && src_w == img_w && src_h == img_h;
    const bool use_cached_fb = full_source && !using_placeholder && image.fit == FitMode::Stretch && cached && cached->fb_img;

    const auto composite_start = std::chrono::steady_clock::now();

    if (use_cached_fb && image.radius <= 0.0f) {
        blend2d_bridge::composite_framebuffer_transformed(fb, *cached->fb_img, scaled_model, final_opacity, BlendMode::Normal, &state);
    } else if (use_cached_fb && image.radius > 0.0f) {
        if (auto rounded = rounded_framebuffer(image.path, *cached, scaled_radius)) {
            blend2d_bridge::composite_framebuffer_transformed(fb, *rounded, scaled_model, final_opacity, BlendMode::Normal, &state);
        }
    } else {
        // Crop/cover/focal placement still needs a materialized source image.
        BLImage sub_img(src_w, src_h, BL_FORMAT_PRGB32);
        BLContext ctx(sub_img);
        ctx.blitImage(BLPointI(0, 0), render_img, BLRectI(src_x, src_y, src_w, src_h));
        ctx.end();
        blend2d_bridge::composite_bl_image_transformed(fb, sub_img, scaled_model, final_opacity, BlendMode::Normal, &state, scaled_radius);
    }

    const auto t_sample1 = std::chrono::steady_clock::now();
    const double sample_ms = std::chrono::duration<double, std::milli>(t_sample1 - composite_start).count();
    const uint64_t sampled_pixels = static_cast<uint64_t>(src_w) * static_cast<uint64_t>(src_h);
    record_image_telemetry(
        state,
        image.path,
        img_w,
        img_h,
        (cached && cached->valid() && !using_placeholder) ? "hit" : "miss_decode",
        decode_ms,
        sample_ms,
        sampled_pixels
    );

    return true;
}

bool ImageRenderer::draw_image_tiled(const ImageShape& image, const RenderState& state, Framebuffer& fb) {
    if (image.path.empty() || image.size.x <= 0 || image.size.y <= 0) {
        return false;
    }

    const CachedImage* cached = nullptr;
    const auto t_decode0 = std::chrono::steady_clock::now();
    cached = ImageCache::instance().get_or_load(image.path);
    const auto t_decode1 = std::chrono::steady_clock::now();
    const double decode_ms = std::chrono::duration<double, std::milli>(t_decode1 - t_decode0).count();
    if (profiling::g_current_counters && decode_ms > 0.0) {
        profiling::g_current_counters->image_decode_ms.fetch_add(
            static_cast<uint64_t>(std::llround(decode_ms)),
            std::memory_order_relaxed
        );
    }

    if (!cached || cached->bl_img.empty()) {
        record_image_telemetry(
            state,
            image.path,
            cached ? cached->width : 0,
            cached ? cached->height : 0,
            "miss_decode",
            decode_ms,
            0.0,
            0
        );
        return false;
    }

    const f32 final_opacity = image.opacity * state.opacity;
    if (final_opacity <= 0.001f) return true;

    const float sx = image.size.x / static_cast<float>(cached->width);
    const float sy = image.size.y / static_cast<float>(cached->height);
    Mat4 scaled_model = state.matrix * glm::scale(Mat4(1.0f), Vec3(sx, sy, 1.0f));

    const bool mask_enabled = state.mask && state.mask->enabled();
    const int clip_x0 = state.clip_rect ? state.clip_rect->x0 : -1;
    const int clip_y0 = state.clip_rect ? state.clip_rect->y0 : -1;
    const int clip_x1 = state.clip_rect ? state.clip_rect->x1 : -1;
    const int clip_y1 = state.clip_rect ? state.clip_rect->y1 : -1;
    spdlog::debug(
        "[image-render-tiled] layer='{}' path='{}' cached={}x{} requested={}x{} scale=({:.4f},{:.4f}) opacity={:.3f} mask={} clip={} clip_rect=[{},{} -> {},{}] tx={:.2f} ty={:.2f}",
        state.layer_id,
        image.path,
        cached->width,
        cached->height,
        image.size.x,
        image.size.y,
        sx,
        sy,
        final_opacity,
        mask_enabled ? 1 : 0,
        state.clip_rect ? 1 : 0,
        clip_x0,
        clip_y0,
        clip_x1,
        clip_y1,
        scaled_model[3][0],
        scaled_model[3][1]
    );

    const auto composite_start = std::chrono::steady_clock::now();

    blend2d_bridge::composite_bl_image_tiled(fb, cached->bl_img, scaled_model, final_opacity, BlendMode::Normal, &state);

    const auto t_sample1 = std::chrono::steady_clock::now();
    const double sample_ms = std::chrono::duration<double, std::milli>(t_sample1 - composite_start).count();
    record_image_telemetry(
        state,
        image.path,
        cached->width,
        cached->height,
        "hit",
        decode_ms,
        sample_ms,
        static_cast<uint64_t>(cached->width) * static_cast<uint64_t>(cached->height)
    );

    return true;
}

} // namespace chronon3d
