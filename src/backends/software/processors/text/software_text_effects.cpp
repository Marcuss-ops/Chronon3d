#include "software_text_effects.hpp"

#include <chronon3d/cache/lru_cache.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include "../../utils/blend2d_bridge.hpp"

#include <blend2d.h>
#include <glm/gtc/matrix_transform.hpp>

#include <cstdlib>
#include <memory>
#include <mutex>

namespace chronon3d::renderer {

using CacheKey = u64;
using ShadowCache = cache::LruCache<CacheKey, std::shared_ptr<BLImage>>;

namespace {

BLRgba32 to_bl_rgba(const Color& c) {
    return BLRgba32(
        static_cast<uint8_t>(std::clamp(c.r * 255.0f, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp(c.g * 255.0f, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp(c.b * 255.0f, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp(c.a * 255.0f, 0.0f, 255.0f))
    );
}

bool is_affine_transform(const Mat4& m) {
    return
        std::abs(m[0][2]) < 1e-5f &&
        std::abs(m[1][2]) < 1e-5f &&
        std::abs(m[2][0]) < 1e-5f &&
        std::abs(m[2][1]) < 1e-5f &&
        std::abs(m[2][2] - 1.0f) < 1e-5f &&
        std::abs(m[2][3]) < 1e-5f &&
        std::abs(m[3][2]) < 1e-5f;
}

bool has_non_translation(const Mat4& m) {
    return
        std::abs(m[0][0] - 1.0f) > 1e-5f ||
        std::abs(m[0][1]) > 1e-5f ||
        std::abs(m[1][0]) > 1e-5f ||
        std::abs(m[1][1] - 1.0f) > 1e-5f;
}

size_t resolve_cache_max_mb(const char* env_name, size_t default_mb) {
    const char* env = std::getenv(env_name);
    if (!env || !*env) return default_mb * 1024ULL * 1024ULL;
    try {
        size_t mb = static_cast<size_t>(std::stoull(env));
        return mb > 0 ? mb * 1024ULL * 1024ULL : default_mb * 1024ULL * 1024ULL;
    } catch (...) {
        return default_mb * 1024ULL * 1024ULL;
    }
}

ShadowCache& get_shadow_cache() {
    static ShadowCache cache(resolve_cache_max_mb("CHRONON_SHADOW_CACHE_MAX_MB", 64), 4);
    return cache;
}

ShadowCache& get_glow_cache() {
    static ShadowCache cache(resolve_cache_max_mb("CHRONON_GLOW_CACHE_MAX_MB", 64), 4);
    return cache;
}

std::mutex g_text_glow_cache_mutex;
std::mutex g_text_shadow_cache_mutex;

using chronon3d::graph::hash_combine;
using chronon3d::graph::hash_value;
using chronon3d::graph::hash_text_style_full;

CacheKey hash_text_shape(const TextShape& text, float effective_size) {
    return hash_text_style_full(text, effective_size, 0);
}

CacheKey hash_glow_params(const RenderNode& node, float effective_size) {
    CacheKey seed = hash_text_shape(node.shape.text, effective_size);
    seed = hash_combine(seed, hash_value(node.glow.radius));
    seed = hash_combine(seed, hash_value(node.glow.intensity));
    seed = hash_combine(seed, hash_value(node.glow.color.r));
    seed = hash_combine(seed, hash_value(node.glow.color.g));
    seed = hash_combine(seed, hash_value(node.glow.color.b));
    seed = hash_combine(seed, hash_value(node.glow.color.a));
    return seed;
}

CacheKey hash_shadow_params(const RenderNode& node, float effective_size, size_t index) {
    CacheKey seed = hash_text_shape(node.shape.text, effective_size);
    seed = hash_combine(seed, hash_value(index));
    const auto& shadow = node.shape.text.style.shadows[index];
    seed = hash_combine(seed, hash_value(shadow.blur));
    seed = hash_combine(seed, hash_value(shadow.opacity));
    seed = hash_combine(seed, hash_value(shadow.color.r));
    seed = hash_combine(seed, hash_value(shadow.color.g));
    seed = hash_combine(seed, hash_value(shadow.color.b));
    seed = hash_combine(seed, hash_value(shadow.color.a));
    return seed;
}

} // namespace

void clear_text_glow_cache() {
    std::lock_guard<std::mutex> lock(g_text_glow_cache_mutex);
    get_glow_cache().clear();
}

void clear_text_shadow_cache() {
    std::lock_guard<std::mutex> lock(g_text_shadow_cache_mutex);
    get_shadow_cache().clear();
}

// ── blur_bl_image_inplace ───────────────────────────────────────────
// Separable box blur directly on BLImage PRGB32 pixels.
// Uses sliding-window sum (O(w×h)) — same algorithm as Framebuffer blur
// but operates on premultiplied-alpha uint32 data, avoiding the
// intermediate Framebuffer allocation + copy in glow/shadow pipelines.

void blur_bl_image_inplace(BLImage& img, float radius) {
    const int r = std::max(1, static_cast<int>(std::round(radius)));

    BLImageData data;
    if (img.getData(&data) != BL_SUCCESS || !data.pixelData) return;

    const int w = data.size.w;
    const int h = data.size.h;
    if (w <= 0 || h <= 0) return;
    const int stride = static_cast<int>(data.stride / sizeof(uint32_t));
    auto* pixels = static_cast<uint32_t*>(data.pixelData);

    // Temporary buffer for horizontal pass result
    std::vector<uint32_t> tmp(static_cast<size_t>(w) * static_cast<size_t>(h));

    // Horizontal pass
    for (int y = 0; y < h; ++y) {
        uint64_t sum_r = 0, sum_g = 0, sum_b = 0, sum_a = 0;
        const uint32_t* row = pixels + y * stride;

        // Initialize sliding window
        for (int x = 0; x <= r && x < w; ++x) {
            const uint32_t p = row[x];
            sum_a += (p >> 24) & 0xFF;
            sum_r += (p >> 16) & 0xFF;
            sum_g += (p >>  8) & 0xFF;
            sum_b +=  p        & 0xFF;
        }
        const int win_size = 2 * r + 1;
        for (int x = 0; x < w; ++x) {
            const uint64_t n = static_cast<uint64_t>(std::min(win_size, std::min(x + r, w - 1) - std::max(x - r, 0) + 1));
            const uint32_t avg_a = static_cast<uint32_t>(sum_a / n);
            const uint32_t avg_r = static_cast<uint32_t>(sum_r / n);
            const uint32_t avg_g = static_cast<uint32_t>(sum_g / n);
            const uint32_t avg_b = static_cast<uint32_t>(sum_b / n);
            tmp[y * w + x] = (avg_a << 24) | (avg_r << 16) | (avg_g << 8) | avg_b;

            // Slide window: remove left, add right
            const int left = x - r;
            if (left >= 0) {
                const uint32_t pl = row[left];
                sum_a -= (pl >> 24) & 0xFF;
                sum_r -= (pl >> 16) & 0xFF;
                sum_g -= (pl >>  8) & 0xFF;
                sum_b -=  pl        & 0xFF;
            }
            const int right = x + r + 1;
            if (right < w) {
                const uint32_t pr = row[right];
                sum_a += (pr >> 24) & 0xFF;
                sum_r += (pr >> 16) & 0xFF;
                sum_g += (pr >>  8) & 0xFF;
                sum_b +=  pr        & 0xFF;
            }
        }
    }

    // Vertical pass (read from tmp, write back to pixels)
    const uint32_t* src = tmp.data();
    for (int x = 0; x < w; ++x) {
        uint64_t sum_r = 0, sum_g = 0, sum_b = 0, sum_a = 0;

        for (int y = 0; y <= r && y < h; ++y) {
            const uint32_t p = src[y * w + x];
            sum_a += (p >> 24) & 0xFF;
            sum_r += (p >> 16) & 0xFF;
            sum_g += (p >>  8) & 0xFF;
            sum_b +=  p        & 0xFF;
        }
        const int win_size = 2 * r + 1;
        for (int y = 0; y < h; ++y) {
            const uint32_t n32 = static_cast<uint32_t>(std::min(win_size, std::min(y + r, h - 1) - std::max(y - r, 0) + 1));
            const uint32_t avg_a = static_cast<uint32_t>(sum_a / n32);
            const uint32_t avg_r = static_cast<uint32_t>(sum_r / n32);
            const uint32_t avg_g = static_cast<uint32_t>(sum_g / n32);
            const uint32_t avg_b = static_cast<uint32_t>(sum_b / n32);
            pixels[y * stride + x] = (avg_a << 24) | (avg_r << 16) | (avg_g << 8) | avg_b;

            const int top = y - r;
            if (top >= 0) {
                const uint32_t pt = src[top * w + x];
                sum_a -= (pt >> 24) & 0xFF;
                sum_r -= (pt >> 16) & 0xFF;
                sum_g -= (pt >>  8) & 0xFF;
                sum_b -=  pt        & 0xFF;
            }
            const int bottom = y + r + 1;
            if (bottom < h) {
                const uint32_t pb = src[bottom * w + x];
                sum_a += (pb >> 24) & 0xFF;
                sum_r += (pb >> 16) & 0xFF;
                sum_g += (pb >>  8) & 0xFF;
                sum_b +=  pb        & 0xFF;
            }
        }
    }
}

} // namespace chronon3d::renderer
