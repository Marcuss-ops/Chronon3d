// ---------------------------------------------------------------------------
// software_text_processor.cpp — Text shape processor
//
// Helper functions (cache, hash, transform utils) → text_processor_helpers.hpp
// Text glow effect → text_glow.cpp
// Text shadow effect → text_shadow.cpp
// ---------------------------------------------------------------------------

#include "software_text_effects.hpp"
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/shape_processor.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/backends/text/text_rasterizer_utils.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include "text_processor_helpers.hpp"
#include "text_effects.hpp"
#include "../../utils/blend2d_bridge.hpp"
#include <blend2d.h>
#include <spdlog/spdlog.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cstdlib>
#include <memory>

namespace chronon3d::renderer {

// ── SoftwareTextProcessor ──────────────────────────────────────────

namespace {

// WP-8 PR 8.1 — ResolverEngineCache (the resolver-keyed FontEngine
// cache from PR 8.0) is RETIRED.  Each SoftwareRenderer now owns its
// own `FontEngine` (initialised from `runtime().resolver()` at
// construction) so the cache-by-resolver-pointer indirection is no
// longer needed.  Per-RenderNode override is preserved via the
// `node.font_engine` field on the render node (PR 8.2 isolation).
// See software_renderer.cpp for the field + init.

class SoftwareTextProcessor final : public ShapeProcessor {
public:
    void draw(SoftwareRenderer& renderer, Framebuffer& fb, const RenderNode& node, const RenderState& state,
              const Camera& camera, i32 width, i32 height) override {
        CHRONON_ZONE_C("text_render", trace_category::kText);
        const bool diagnostics_enabled = renderer.is_diagnostic_mode();
        const auto draw_start = diagnostics_enabled ? profiling::now() : profiling::Clock::time_point{};
        const Mat4& model = state.matrix;
        const f32 opacity = state.opacity;
        const float effective_size = node.shape.text().style.size * state.ssaa_factor;

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
        const auto raster_start = diagnostics_enabled ? profiling::now() : profiling::Clock::time_point{};
        // WP-8 PR 8.1 — FontEngine is sourced from the per-renderer
        // `SoftwareRenderer::font_engine()` accessor (the PR 8.0/8.1
        // hoist).  `node.font_engine` overrides per-RenderNode when bound
        // (PR 8.2 isolation contract); otherwise the renderer-local
        // engine is the canonical source.  No more resolver-keyed static
        // cache; no more per-call construction.
        //
        // The function-scope resolver stays so `rasterize_text_to_bl_image`
        // receives it explicitly below.  `AssetResolver` is a value-member
        // of `RenderRuntime`, so the reference is stable for the
        // renderer's lifetime.
        const auto& resolver = renderer.runtime().resolver();
        FontEngine* engine = node.font_engine ? node.font_engine : &renderer.font_engine();
        // TICKET-007: per-instance DebugConfig forwarded from the
        // owning SoftwareRenderer so text-bbox / ink-bounds / baseline
        // debug overlays honour the engine's debug.text_bbox() flag
        // and never read a process-wide singleton.
        const chronon3d::DebugConfig* text_debug_cfg = &renderer.config().debug();
        auto raster = rasterize_text_to_bl_image(node.shape.text(), effective_size, 32, resolver, &raster_cache_hit, raster_transform, engine, text_debug_cfg);
        if (diagnostics_enabled) {
            rasterize_ms = profiling::elapsed_ms(raster_start);
        }
        if (!raster) {
            spdlog::warn("Text rasterization failed for node '{}'", node.name);
            return;
        }

        if (renderer.config().debug().dump_text_raster()) {
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
                static_cast<uint64_t>(node.shape.text().text.length()),
                std::memory_order_relaxed
            );
        }

        // ── Apply TextMaterial (gradient, bevel, highlight, shade) ─────
        if (node.shape.text().style.material.enabled) {
            apply_text_material(raster->image, node.shape.text().style.material);
        }

        double shadow_ms = 0.0;
        double glow_ms = 0.0;
        double composite_ms = 0.0;

        // 1. Drop Shadows (behind)
        for (size_t i = 0; i < node.shape.text().style.shadows.size(); ++i) {
            const auto& shadow = node.shape.text().style.shadows[i];
            if (shadow.enabled && shadow.opacity > 0.0f && shadow.color.a > 0.0f) {
                const auto shadow_start = diagnostics_enabled ? profiling::now() : profiling::Clock::time_point{};
                draw_text_shadow(renderer, fb, node, state, *raster, shadow, i, effective_size);
                if (diagnostics_enabled) {
                    shadow_ms += profiling::elapsed_ms(shadow_start);
                }
            }
        }

        // 2. Glow
        if (node.glow.enabled && node.glow.intensity > 0.0f && node.glow.color.a > 0.0f) {
            const auto glow_start = diagnostics_enabled ? profiling::now() : profiling::Clock::time_point{};
            draw_text_glow(renderer, fb, node, state, *raster, effective_size);
            if (diagnostics_enabled) {
                glow_ms += profiling::elapsed_ms(glow_start);
            }
        }

        // 3. Text
        const auto composite_start = diagnostics_enabled ? profiling::now() : profiling::Clock::time_point{};
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
            composite_ms += profiling::elapsed_ms(composite_start);
            const double total_ms = profiling::elapsed_ms(draw_start);
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
        const auto& txt = shape.text();
        f32 w = 400.0f;
        f32 h = 200.0f;
        if (txt.box.enabled && txt.box.size.x > 0.0f && txt.box.size.y > 0.0f) {
            w = txt.box.size.x;
            h = txt.box.size.y;
        } else if (!txt.text.empty()) {
            const float font_size = std::max(1.0f, txt.style.size);
            const float line_height = font_size * std::max(1.0f, txt.style.line_height);

            // WP-8 PR 8.1 — per-processor static FontEngine, sourced
            // from the post-bridge process-wide resolver channel.
            // This `compute_world_bbox` virtual doesn't carry a
            // `SoftwareRenderer&` argument (signature is fixed), so we
            // can't use the per-renderer `SoftwareRenderer::font_engine()`
            // accessor that PR 8.1 introduced for the `draw()` path.
            //
            // Function-local static lifetime: the engine's resolver
            // pointer is captured on first call.  Subsequent calls
            // reuse the same engine — per-engine asset-isolation holds
            // because `AssetResolver` is a value member of
            // `RenderRuntime`, and the process-wide resolver is mounted
            // by `RenderRuntime::attach_assets_root` → `resolver().mount()`
            // at engine init, so its mount tracks the same root the
            // runtime is currently exposing.
            //
            // The previous ternary (active_runtime() → bridge) is
            // RETIRED: WP-8 PR 8.1 deletes both `active_runtime()` and
            // `runtime::typed_resolver_for_deep_code()`.  Any test/
            // CLI/context path that needs the resolver WITHOUT an
            // explicit runtime reference now uses this free function.
            const chronon3d::assets::AssetResolver& resolver =
                chronon3d::runtime::process_wide_resolver();
            static const FontEngine bbox_engine{resolver};
            const FontEngine& engine = bbox_engine;
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

std::unique_ptr<ShapeProcessor> create_text_processor() {
    return std::make_unique<SoftwareTextProcessor>();
}

} // namespace chronon3d::renderer
