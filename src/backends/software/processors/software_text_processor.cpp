// software_text_processor.cpp
// Text shape processor: rasterization, glow, shadow, caching, and hash utilities.
// Consolidated from: software_text_processor.cpp, _cache.cpp, _hash.cpp,
// _glow.cpp, _shadow.cpp and software_text_processor_internal.hpp.

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/shape_processor.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/backends/text/text_rasterizer_utils.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/render_graph/render_graph_hashing.hpp>
#include <chronon3d/cache/lru_cache.hpp>
#include "../utils/blend2d_bridge.hpp"
#include <blend2d.h>
#include <spdlog/spdlog.h>
#include <glm/gtc/matrix_transform.hpp>
#include <chrono>
#include <mutex>
#include <cstdlib>
#include <memory>

namespace chronon3d::renderer {

using CacheKey = u64;
using ShadowCache = cache::LruCache<CacheKey, std::shared_ptr<BLImage>>;

// ── helpers ───────────────────────────────────────────────────────

inline BLRgba32 to_bl_rgba(const Color& c) {
    return BLRgba32(
        static_cast<uint8_t>(std::clamp(c.r * 255.0f, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp(c.g * 255.0f, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp(c.b * 255.0f, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp(c.a * 255.0f, 0.0f, 255.0f))
    );
}

// ── forward declarations ───────────────────────────────────────────

static void draw_text_shadow(SoftwareRenderer& renderer, Framebuffer& fb, const RenderNode& node,
                             const RenderState& state, const TextRasterization& raster,
                             const TextShadow& shadow, size_t index, float effective_size);
static void draw_text_glow(SoftwareRenderer& renderer, Framebuffer& fb, const RenderNode& node,
                           const RenderState& state, const TextRasterization& raster,
                           float effective_size);

// ── transform utilities ────────────────────────────────────────────

static bool is_affine_transform(const Mat4& m) {
    return
        std::abs(m[0][2]) < 1e-5f &&
        std::abs(m[1][2]) < 1e-5f &&
        std::abs(m[2][0]) < 1e-5f &&
        std::abs(m[2][1]) < 1e-5f &&
        std::abs(m[2][2] - 1.0f) < 1e-5f &&
        std::abs(m[2][3]) < 1e-5f &&
        std::abs(m[3][2]) < 1e-5f;
}

static bool has_non_translation(const Mat4& m) {
    return
        std::abs(m[0][0] - 1.0f) > 1e-5f ||
        std::abs(m[0][1]) > 1e-5f ||
        std::abs(m[1][0]) > 1e-5f ||
        std::abs(m[1][1] - 1.0f) > 1e-5f;
}

// ── cache management ───────────────────────────────────────────────

static size_t resolve_cache_max_mb(const char* env_name, size_t default_mb) {
    const char* env = std::getenv(env_name);
    if (!env || !*env) return default_mb * 1024ULL * 1024ULL;
    try {
        size_t mb = static_cast<size_t>(std::stoull(env));
        return mb > 0 ? mb * 1024ULL * 1024ULL : default_mb * 1024ULL * 1024ULL;
    } catch (...) {
        return default_mb * 1024ULL * 1024ULL;
    }
}

static ShadowCache& get_shadow_cache() {
    static ShadowCache cache(resolve_cache_max_mb("CHRONON_SHADOW_CACHE_MAX_MB", 64), 4);
    return cache;
}

static ShadowCache& get_glow_cache() {
    static ShadowCache cache(resolve_cache_max_mb("CHRONON_GLOW_CACHE_MAX_MB", 64), 4);
    return cache;
}

static std::mutex g_text_glow_cache_mutex;
static std::mutex g_text_shadow_cache_mutex;

// ── hash utilities ─────────────────────────────────────────────────

using chronon3d::graph::hash_combine;
using chronon3d::graph::hash_value;
using chronon3d::graph::hash_string;
using chronon3d::graph::hash_text_style_full;

static CacheKey hash_text_shape(const TextShape& text, float effective_size) {
    return hash_text_style_full(text, effective_size, 0);
}

static CacheKey hash_glow_params(const RenderNode& node, float effective_size) {
    CacheKey seed = hash_text_shape(node.shape.text, effective_size);
    seed = hash_combine(seed, hash_value(node.glow.radius));
    seed = hash_combine(seed, hash_value(node.glow.intensity));
    seed = hash_combine(seed, hash_value(node.glow.color.r));
    seed = hash_combine(seed, hash_value(node.glow.color.g));
    seed = hash_combine(seed, hash_value(node.glow.color.b));
    seed = hash_combine(seed, hash_value(node.glow.color.a));
    return seed;
}

static CacheKey hash_shadow_params(const RenderNode& node, float effective_size, size_t index) {
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

// ── glow ───────────────────────────────────────────────────────────

static void draw_text_glow(SoftwareRenderer& renderer, Framebuffer& fb, const RenderNode& node,
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
        std::lock_guard<std::mutex> lock(g_text_glow_cache_mutex);
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
        BLImage glow_img;
        glow_img.create(raster.image.width(), raster.image.height(), BL_FORMAT_PRGB32);
        {
            BLContext ctx(glow_img);
            ctx.setCompOp(BL_COMP_OP_SRC_COPY);
            ctx.setFillStyle(BLRgba32(0, 0, 0, 0));
            ctx.fillAll();
            ctx.blitImage(BLPoint(0, 0), raster.image);
            ctx.setCompOp(BL_COMP_OP_SRC_IN);
            Color glow_color = node.glow.color;
            glow_color.a = 1.0f;
            ctx.setFillStyle(to_bl_rgba(glow_color));
            ctx.fillAll();
        }

        auto cached_img = std::make_shared<BLImage>(glow_img);

        if (node.glow.radius > 0.0f) {
            auto glow_fb = renderer.framebuffer_pool()->acquire(glow_img.width(), glow_img.height(), true);
            chronon3d::blend2d_bridge::composite_bl_image(*glow_fb, glow_img, 0, 0, 1.0f, BlendMode::Normal);
            renderer.apply_blur(*glow_fb, node.glow.radius);
            
            const f32 glow_intensity_opacity = node.glow.intensity * node.glow.color.a;
            if (use_geo_transform) {
                int x = static_cast<int>(std::lround(raster.x_offset));
                int y = static_cast<int>(std::lround(raster.y_offset));
                chronon3d::blend2d_bridge::composite_framebuffer(fb, *glow_fb, x, y, opacity * glow_intensity_opacity, BlendMode::Add);
            } else {
                Mat4 glow_model = model * glm::translate(Mat4(1.0f), Vec3(raster.x_offset, raster.y_offset, 0.0f));
                chronon3d::blend2d_bridge::composite_framebuffer_transformed(fb, *glow_fb, glow_model, opacity * glow_intensity_opacity, BlendMode::Add);
            }
            return;
        }

        if (cached_img) {
            std::lock_guard<std::mutex> lock(g_text_glow_cache_mutex);
            size_t weight = cached_img->width() * cached_img->height() * 4;
            get_glow_cache().put(key, cached_img, weight);
            glow_cache = cached_img;
        }
    }

    const f32 glow_intensity_opacity = node.glow.intensity * node.glow.color.a;

    if (glow_cache) {
        if (use_geo_transform) {
            int x = static_cast<int>(std::lround(raster.x_offset));
            int y = static_cast<int>(std::lround(raster.y_offset));
            chronon3d::blend2d_bridge::composite_bl_image(fb, *glow_cache, x, y, opacity * glow_intensity_opacity, BlendMode::Add);
        } else {
            Mat4 glow_model = model * glm::translate(Mat4(1.0f), Vec3(raster.x_offset, raster.y_offset, 0.0f));
            chronon3d::blend2d_bridge::composite_bl_image_transformed(fb, *glow_cache, glow_model, opacity * glow_intensity_opacity, BlendMode::Add);
        }
    }
}

// ── shadow ─────────────────────────────────────────────────────────

static void draw_text_shadow(SoftwareRenderer& renderer, Framebuffer& fb, const RenderNode& node,
                             const RenderState& state, const TextRasterization& raster,
                             const TextShadow& shadow, size_t index, float effective_size) {
    CHRONON_ZONE_C("text_shadow", trace_category::kText);
    const Mat4& model = state.matrix;
    const f32 opacity = state.opacity;

    const bool use_geo_transform = !state.projection.ready &&
                                   is_affine_transform(model) &&
                                   has_non_translation(model);

    const CacheKey key = hash_shadow_params(node, effective_size, index);
    std::shared_ptr<BLImage> shadow_cache;
    {
        std::lock_guard<std::mutex> lock(g_text_shadow_cache_mutex);
        auto cached = get_shadow_cache().get(key);
        if (cached) {
            shadow_cache = *cached;
            if (profiling::g_current_counters) {
                profiling::g_current_counters->text_shadow_cache_hits.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }

    if (!shadow_cache) {
        if (profiling::g_current_counters) {
            profiling::g_current_counters->text_shadow_cache_misses.fetch_add(1, std::memory_order_relaxed);
        }
        BLImage shadow_img;
        shadow_img.create(raster.image.width(), raster.image.height(), BL_FORMAT_PRGB32);
        {
            BLContext ctx(shadow_img);
            ctx.setCompOp(BL_COMP_OP_SRC_COPY);
            ctx.setFillStyle(BLRgba32(0, 0, 0, 0));
            ctx.fillAll();
            ctx.blitImage(BLPoint(0, 0), raster.image);
            ctx.setCompOp(BL_COMP_OP_SRC_IN);
            Color shadow_color_tint = shadow.color;
            shadow_color_tint.a = 1.0f;
            ctx.setFillStyle(to_bl_rgba(shadow_color_tint));
            ctx.fillAll();
        }

        auto cached_img = std::make_shared<BLImage>(shadow_img);

        if (shadow.blur > 0.0f) {
            auto shadow_fb = renderer.framebuffer_pool()->acquire(shadow_img.width(), shadow_img.height(), true);
            chronon3d::blend2d_bridge::composite_bl_image(*shadow_fb, shadow_img, 0, 0, 1.0f, BlendMode::Normal);
            renderer.apply_blur(*shadow_fb, shadow.blur);
            
            const f32 shadow_opacity = shadow.opacity * shadow.color.a;
            if (use_geo_transform) {
                int x = static_cast<int>(std::lround(raster.x_offset + shadow.offset.x));
                int y = static_cast<int>(std::lround(raster.y_offset + shadow.offset.y));
                chronon3d::blend2d_bridge::composite_framebuffer(fb, *shadow_fb, x, y, opacity * shadow_opacity, BlendMode::Normal);
            } else {
                Mat4 shadow_model = model * glm::translate(Mat4(1.0f), Vec3(raster.x_offset + shadow.offset.x, raster.y_offset + shadow.offset.y, 0.0f));
                chronon3d::blend2d_bridge::composite_framebuffer_transformed(fb, *shadow_fb, shadow_model, opacity * shadow_opacity, BlendMode::Normal);
            }
            return;
        }

        if (cached_img) {
            std::lock_guard<std::mutex> lock(g_text_shadow_cache_mutex);
            size_t weight = cached_img->width() * cached_img->height() * 4;
            get_shadow_cache().put(key, cached_img, weight);
            shadow_cache = cached_img;
        }
    }

    const f32 shadow_opacity = shadow.opacity * shadow.color.a;

    if (shadow_cache) {
        if (use_geo_transform) {
            int x = static_cast<int>(std::lround(raster.x_offset + shadow.offset.x));
            int y = static_cast<int>(std::lround(raster.y_offset + shadow.offset.y));
            chronon3d::blend2d_bridge::composite_bl_image(fb, *shadow_cache, x, y, opacity * shadow_opacity, BlendMode::Normal);
        } else {
            Mat4 shadow_model = model * glm::translate(Mat4(1.0f), Vec3(raster.x_offset + shadow.offset.x, raster.y_offset + shadow.offset.y, 0.0f));
            chronon3d::blend2d_bridge::composite_bl_image_transformed(fb, *shadow_cache, shadow_model, opacity * shadow_opacity, BlendMode::Normal);
        }
    }
}

// ── SoftwareTextProcessor ──────────────────────────────────────────

namespace {

class SoftwareTextProcessor final : public ShapeProcessor {
public:
    void draw(SoftwareRenderer& renderer, Framebuffer& fb, const RenderNode& node, const RenderState& state,
              const Camera& camera, i32 width, i32 height) override {
        CHRONON_ZONE_C("text_render", trace_category::kText);
        const bool diagnostics_enabled = renderer.is_diagnostic_mode();
        const auto draw_start = diagnostics_enabled ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        const Mat4& model = state.matrix;
        const f32 opacity = state.opacity;
        const float effective_size = node.shape.text.style.size * state.ssaa_factor;

        const bool use_geo_transform = !state.projection.ready &&
                                       is_affine_transform(model) &&
                                       has_non_translation(model);

        const Mat4* raster_transform = nullptr;
        Mat4 text_model_storage;

        if (use_geo_transform) {
            text_model_storage = model;
            raster_transform = &text_model_storage;
        }

        bool raster_cache_hit = false;
        double rasterize_ms = 0.0;
        const auto raster_start = diagnostics_enabled ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        FontEngine* engine = node.font_engine ? node.font_engine : &shared_font_engine();
        auto raster = rasterize_text_to_bl_image(node.shape.text, effective_size, 32, &raster_cache_hit, raster_transform, engine);
        if (diagnostics_enabled) {
            rasterize_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - raster_start).count();
        }
        if (!raster) {
            spdlog::warn("Text rasterization failed for node '{}'", node.name);
            return;
        }

        if (std::getenv("CHRONON_DEBUG_DUMP_TEXT_RASTER")) {
            BLImageData debug_data;
            if (raster->image.getData(&debug_data) == BL_SUCCESS) {
                const int sw = debug_data.size.w;
                const int sh = debug_data.size.h;
                const int stride = static_cast<int>(debug_data.stride / sizeof(uint32_t));
                Framebuffer debug_fb(sw, sh);
                const uint32_t* src_pixels = reinterpret_cast<const uint32_t*>(debug_data.pixelData);
                for (int y = 0; y < sh; ++y) {
                    for (int x = 0; x < sw; ++x) {
                        const uint32_t p = src_pixels[y * stride + x];
                        debug_fb.set_pixel(
                            x,
                            y,
                            Color{
                                static_cast<float>((p >> 16) & 0xFF) / 255.0f,
                                static_cast<float>((p >> 8) & 0xFF) / 255.0f,
                                static_cast<float>(p & 0xFF) / 255.0f,
                                static_cast<float>((p >> 24) & 0xFF) / 255.0f
                            }
                        );
                    }
                }
                save_png(debug_fb, "output/debug_text_raster.png");
            }
        }

        if (!raster_cache_hit) {
            renderer.counters()->text_glyphs_rasterized.fetch_add(
                static_cast<uint64_t>(node.shape.text.text.length()),
                std::memory_order_relaxed
            );
        }

        // ── Apply TextMaterial (gradient, bevel, highlight, shade) ─────
        if (node.shape.text.style.material.enabled) {
            apply_text_material(raster->image, node.shape.text.style.material);
        }

        double shadow_ms = 0.0;
        double glow_ms = 0.0;
        double composite_ms = 0.0;

        // 1. Drop Shadows (behind)
        for (size_t i = 0; i < node.shape.text.style.shadows.size(); ++i) {
            const auto& shadow = node.shape.text.style.shadows[i];
            if (shadow.enabled && shadow.opacity > 0.0f && shadow.color.a > 0.0f) {
                const auto shadow_start = diagnostics_enabled ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
                draw_text_shadow(renderer, fb, node, state, *raster, shadow, i, effective_size);
                if (diagnostics_enabled) {
                    shadow_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - shadow_start).count();
                }
            }
        }

        // 2. Glow
        if (node.glow.enabled && node.glow.intensity > 0.0f && node.glow.color.a > 0.0f) {
            const auto glow_start = diagnostics_enabled ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
            draw_text_glow(renderer, fb, node, state, *raster, effective_size);
            if (diagnostics_enabled) {
                glow_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - glow_start).count();
            }
        }

        // 3. Text
        const auto composite_start = diagnostics_enabled ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        if (use_geo_transform) {
            int x = static_cast<int>(std::lround(raster->x_offset));
            int y = static_cast<int>(std::lround(raster->y_offset));
            chronon3d::blend2d_bridge::composite_bl_image(fb, raster->image, x, y, opacity, BlendMode::Normal);
        } else {
            const int x = static_cast<int>(std::lround(model[3][0] + raster->x_offset));
            const int y = static_cast<int>(std::lround(model[3][1] + raster->y_offset));
            chronon3d::blend2d_bridge::composite_bl_image(fb, raster->image, x, y, opacity, BlendMode::Normal);
        }
        if (diagnostics_enabled) {
            composite_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - composite_start).count();
            const double total_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - draw_start).count();
            spdlog::info(
                "[TextRender] node='{}' cache_hit={} rasterize={:.2f}ms shadow={:.2f}ms glow={:.2f}ms composite={:.2f}ms total={:.2f}ms",
                node.name,
                raster_cache_hit,
                rasterize_ms,
                shadow_ms,
                glow_ms,
                composite_ms,
                total_ms
            );
        }
    }

    raster::BBox compute_world_bbox(const Shape& shape, const Mat4& model, f32 spread) override {
        const auto& txt = shape.text;
        f32 w = 400.0f;
        f32 h = 200.0f;
        if (txt.box.enabled && txt.box.size.x > 0.0f && txt.box.size.y > 0.0f) {
            w = txt.box.size.x;
            h = txt.box.size.y;
        } else if (!txt.text.empty()) {
            const float font_size = std::max(1.0f, txt.style.size);
            const float line_height = font_size * std::max(1.0f, txt.style.line_height);

            // Use the shared FontEngine singleton for accurate per-line
            // measurement (compute_world_bbox does not have access to the
            // RenderNode, so it uses the process-wide shared instance).
            FontEngine& engine = shared_font_engine();
            FontSpec spec;
            spec.font_path   = txt.style.font_path;
            spec.font_family = txt.style.font_family;
            spec.font_weight = txt.style.font_weight;
            spec.font_style  = txt.style.font_style;

            int line_count = 1;
            float max_line_w = 0.0f;
            std::string_view sv = txt.text;
            size_t start = 0;
            for (size_t i = 0; i <= sv.size(); ++i) {
                if (i == sv.size() || sv[i] == '\n') {
                    const size_t line_len = i - start;
                    float line_w = engine.measure_text(sv.substr(start, line_len), spec, font_size);
                    if (line_w <= 0.0f) {
                        // FontEngine failed to load the face; fall back to legacy approximation
                        line_w = static_cast<float>(line_len) * font_size * 0.6f;
                    }
                    line_w += txt.style.tracking * static_cast<float>(line_len);
                    max_line_w = std::max(max_line_w, line_w);
                    start = i + 1;
                    if (i < sv.size()) ++line_count;
                }
            }

            w = max_line_w;
            h = static_cast<float>(line_count) * line_height;
            if (txt.style.box_style.enabled) {
                w += 2.0f * txt.style.box_style.padding.x;
                h += 2.0f * txt.style.box_style.padding.y;
            }
        }

        Vec4 corners[4] = {
            model * Vec4(0.0f, 0.0f, 0.0f, 1.0f),
            model * Vec4(w,    0.0f, 0.0f, 1.0f),
            model * Vec4(w,    h,    0.0f, 1.0f),
            model * Vec4(0.0f, h,    0.0f, 1.0f)
        };

        f32 min_x = 1e10f, max_x = -1e10f;
        f32 min_y = 1e10f, max_y = -1e10f;
        for (const auto& c : corners) {
            if (std::abs(c.w) > 1e-7f) {
                const f32 x = c.x / c.w;
                const f32 y = c.y / c.w;
                min_x = std::min(min_x, x);
                max_x = std::max(max_x, x);
                min_y = std::min(min_y, y);
                max_y = std::max(max_y, y);
            } else {
                min_x = std::min(min_x, c.x);
                max_x = std::max(max_x, c.x);
                min_y = std::min(min_y, c.y);
                max_y = std::max(max_y, c.y);
            }
        }

        const f32 pad = spread + 20.0f;

        return {
            static_cast<i32>(std::floor(min_x - pad)),
            static_cast<i32>(std::floor(min_y - pad)),
            static_cast<i32>(std::ceil(max_x + pad)),
            static_cast<i32>(std::ceil(max_y + pad))
        };
    }

    bool hit_test(const Shape& shape, Vec2 local_point, f32 spread) override {
        return false;
    }
};

} // anonymous namespace

// ── public API ───────────────────────────────────────────────────

void clear_text_glow_cache() {
    std::lock_guard<std::mutex> lock(g_text_glow_cache_mutex);
    get_glow_cache().clear();
}

void clear_text_shadow_cache() {
    std::lock_guard<std::mutex> lock(g_text_shadow_cache_mutex);
    get_shadow_cache().clear();
}

std::unique_ptr<ShapeProcessor> create_text_processor() {
    return std::make_unique<SoftwareTextProcessor>();
}

} // namespace chronon3d::renderer
