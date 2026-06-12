#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <mutex>
#include <unordered_map>
#include <optional>
#include <vector>

#include <chronon3d/assets/asset_registry.hpp>
#include <chronon3d/backends/text/text_rasterizer_utils.hpp>
#include <chronon3d/backends/text/text_layout_engine.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/core/config.hpp>
#include <blend2d/gradient.h>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/counters.hpp>

namespace chronon3d {

namespace {

inline BLRgba32 to_bl_rgba(const Color& c) {
    return BLRgba32(
        static_cast<uint8_t>(std::clamp(c.r * 255.0f, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp(c.g * 255.0f, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp(c.b * 255.0f, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp(c.a * 255.0f, 0.0f, 255.0f))
    );
}

inline void apply_text_fill_style(
    BLContext& ctx,
    const TextStyle& style,
    const Color& fallback_color,
    float origin_x,
    float origin_y,
    float width,
    float height
) {
    if (!style.paint.fill_style.has_value()) {
        ctx.setFillStyle(to_bl_rgba(fallback_color));
        return;
    }

    const Fill& fill = *style.paint.fill_style;
    if (fill.type == FillType::Solid) {
        ctx.setFillStyle(to_bl_rgba(fill.solid));
        return;
    }

    std::vector<BLGradientStop> stops;
    stops.reserve(fill.gradient.stops.size());
    for (const auto& stop : fill.gradient.stops) {
        stops.emplace_back(static_cast<double>(stop.offset), to_bl_rgba(stop.color));
    }

    if (stops.empty()) {
        ctx.setFillStyle(to_bl_rgba(fallback_color));
        return;
    }

    const float safe_w = std::max(1.0f, width);
    const float safe_h = std::max(1.0f, height);

    if (fill.type == FillType::LinearGradient) {
        const BLLinearGradientValues values(
            origin_x + fill.gradient.from.x * safe_w,
            origin_y + fill.gradient.from.y * safe_h,
            origin_x + fill.gradient.to.x * safe_w,
            origin_y + fill.gradient.to.y * safe_h
        );
        BLGradient gradient(values, BL_EXTEND_MODE_PAD, stops.data(), stops.size());
        ctx.setFillStyle(gradient);
        return;
    }

    if (fill.type == FillType::RadialGradient) {
        const float radius_norm = std::max(0.001f, fill.gradient.to.x - fill.gradient.from.x);
        const float radius = std::max(safe_w, safe_h) * radius_norm;
        const BLRadialGradientValues values(
            origin_x + fill.gradient.from.x * safe_w,
            origin_y + fill.gradient.from.y * safe_h,
            origin_x + fill.gradient.from.x * safe_w,
            origin_y + fill.gradient.from.y * safe_h,
            0.0,
            radius
        );
        BLGradient gradient(values, BL_EXTEND_MODE_PAD, stops.data(), stops.size());
        ctx.setFillStyle(gradient);
        return;
    }

    if (fill.type == FillType::ConicGradient) {
        const double cx = origin_x + fill.gradient.from.x * safe_w;
        const double cy = origin_y + fill.gradient.from.y * safe_h;
        const Vec2 dir = fill.gradient.to - fill.gradient.from;
        const double angle = std::atan2(dir.y, dir.x);
        const BLConicGradientValues values(cx, cy, angle);
        BLGradient gradient(values, BL_EXTEND_MODE_PAD, stops.data(), stops.size());
        ctx.setFillStyle(gradient);
        return;
    }

    ctx.setFillStyle(to_bl_rgba(fallback_color));
}

struct Blend2DResources {
    std::unordered_map<std::string, BLFontFace> faces;
    std::mutex mutex;

    BLFontFace get_face(const std::string& path) {
        std::lock_guard<std::mutex> lock(mutex);
        const std::string resolved_path = AssetRegistry::resolve(path);
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

} // namespace

using CacheKey = u64;

CacheKey hash_text_style(const TextShape& t, float effective_size, int padding, const Mat4* transform);
bool lookup_text_cache(const CacheKey& key, std::shared_ptr<TextRasterization>& out);
void store_text_cache(const CacheKey& key, const std::shared_ptr<TextRasterization>& result);

// Blend2D-based text measurement callback for the layout engine.
// Uses the same engine as rasterization, eliminating the FT+HB vs B2D
// width discrepancy that required pixel-ink centering corrections.
namespace {
float bl2d_measure_text(const void* font_face_ptr, std::string_view text, float font_size) {
    auto* face = static_cast<const BLFontFace*>(font_face_ptr);
    BLFont f;
    f.createFromFace(*face, std::max(1.0f, font_size));
    BLGlyphBuffer gb;
    gb.setUtf8Text(text.data(), static_cast<int>(text.size()));
    f.shape(gb);
    BLTextMetrics m;
    f.getTextMetrics(gb, m);
    return m.boundingBox.x1 - m.boundingBox.x0;
}
} // namespace

std::optional<TextRasterization> rasterize_text_to_bl_image(
    const TextShape& t,
    float effective_size,
    int padding,
    bool* cache_hit,
    const Mat4* transform,
    FontEngine* font_engine
) {
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

    BLFontFace face = blend2d_resources().get_face(font_path);
    if (face.empty()) return std::nullopt;

    BLFont font;
    font.createFromFace(face, effective_size);

    TextBox layout_box = t.box;
    if (t.style.box_style.enabled && t.box.enabled) {
        layout_box.size.x = std::max(0.0f, t.box.size.x - 2.0f * t.style.box_style.padding.x);
        layout_box.size.y = std::max(0.0f, t.box.size.y - 2.0f * t.style.box_style.padding.y);
    }

    // Reuse the caller-provided FontEngine if available; otherwise fall back
    // to a local instance (preserves backward compatibility for tests &
    // direct API callers).
    FontEngine local_engine;
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

    // Use Blend2D for text measurement — same engine as rasterization.
    // This eliminates the FreeType+HarfBuzz vs Blend2D width discrepancy
    // and makes the pixel-ink centering correction unnecessary.
    layout_in.bl_font_ptr = &face;
    layout_in.bl_measure_fn = bl2d_measure_text;

    auto start_layout = profiling::now();
    auto layout_res = TextLayoutEngine::layout(layout_in);
        if (profiling::g_current_counters) {
        auto dur = static_cast<uint64_t>(profiling::elapsed_ms(start_layout));
        profiling::g_current_counters->text_layout_ms.fetch_add(static_cast<uint64_t>(dur), std::memory_order_relaxed);
    }

    // Recreate the font at the final resolved size from the layout engine
    font.createFromFace(face, layout_res.font_size);



    int tw = 0;
    int th = 0;
    if (t.box.enabled) {
        // Use the larger of box size and actual layout size so wrapped text
        // is never clipped vertically (or horizontally).
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
    // With Blend2D measurement now used for layout (bl_measure_fn), the
    // measurement engine matches the rendering engine, so dx_align should
    // be accurate.  The pixel-ink centering below is kept as a safety net.
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
    // Guard: clamp text_start_x so text is always within the image.
    // With Blend2D measurement, this should rarely trigger.
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
        return blend2d_resources().get_face(path);
    };

    auto render_run = [&](const TextLayoutLineRun& run, const TextLayoutLine& line) {
        if (run.text.empty()) {
            return;
        }

        TextStyle run_style = run.style;
        if (run_style.font_path.empty()) run_style.font_path = t.style.font_path;
        if (run_style.font_family.empty()) run_style.font_family = t.style.font_family;
        if (run_style.font_style.empty()) run_style.font_style = t.style.font_style;
        if (run_style.font_weight == 0) run_style.font_weight = t.style.font_weight;
        if (run_style.size <= 0.0f) run_style.size = layout_res.font_size;
        if (run_style.line_height <= 0.0f) run_style.line_height = t.style.line_height;

        BLFontFace run_face = resolve_font_face(run_style);
        if (run_face.empty()) {
            return;
        }

        BLFont run_font;
        run_font.createFromFace(run_face, std::max(1.0f, run_style.size));

        const float baseline_y = text_start_y + line.position.y + line.baseline;
        const float lx = text_start_x + run.position.x;
        const Color run_fill = resolve_fill_color(run_style);

        if (run_style.paint.stroke_enabled && run_style.paint.stroke_width > 0.0f) {
            ctx.setStrokeWidth(run_style.paint.stroke_width);
            ctx.setStrokeStyle(to_bl_rgba(run_style.paint.stroke_color));
            ctx.strokeUtf8Text(BLPoint(lx, baseline_y), run_font, run.text.c_str());
        }

        apply_text_fill_style(
            ctx,
            run_style,
            run_fill,
            text_start_x,
            text_start_y,
            std::max(1.0f, run.width),
            line.baseline + line.descent
        );
        ctx.fillUtf8Text(BLPoint(lx, baseline_y), run_font, run.text.c_str());
    };

    for (const auto& line : layout_res.lines) {
        if (line.text.empty()) continue;

        if (line.runs.size() > 1) {
            for (const auto& run : line.runs) {
                render_run(run, line);
            }
            continue;
        }

        const float lx = text_start_x + line.position.x;
        const float ly = text_start_y + line.position.y + line.baseline;
        const Color line_fill = resolve_fill_color(t.style);

        if (t.style.paint.stroke_enabled && t.style.paint.stroke_width > 0.0f) {
            ctx.setStrokeWidth(t.style.paint.stroke_width);
            ctx.setStrokeStyle(to_bl_rgba(t.style.paint.stroke_color));
            ctx.strokeUtf8Text(BLPoint(lx, ly), font, line.text.c_str());
        }

        apply_text_fill_style(
            ctx,
            t.style,
            line_fill,
            text_start_x,
            text_start_y,
            text_block_w,
            text_block_h
        );
        ctx.fillUtf8Text(BLPoint(lx, ly), font, line.text.c_str());
    }

    ctx.end();

    // ── Trim trailing rows AND compute ink-bounds for centering ───────
    float ink_center_frac = -1.0f; // -1 = unknown / no ink
    float ink_w = 0.0f;
    int ink_left = 0, ink_right = 0, ink_top = 0, ink_bottom = 0;
    {
        BLImageData trim_data;
        if (img.getData(&trim_data) == BL_SUCCESS && trim_data.pixelData && trim_data.size.h > 0) {
            const int sw = trim_data.size.w;
            const int sh = trim_data.size.h;
            const int stride = static_cast<int>(trim_data.stride / sizeof(uint32_t));
            auto* pixels = reinterpret_cast<uint32_t*>(trim_data.pixelData);

            // 1. Count non-zero-alpha pixels per row and find the maximum.
            //    Also track the overall ink column bounds.
            //    Sampled scan: stride 4 horizontally, stride 2 vertically
            //    (8x fewer pixels vs full scan; bounds expanded to compensate).
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
            // Expand sampled ink bounds to cover the stride gaps.
            // A non-zero pixel at (x,y) means content could extend to
            // (x - kSampleStrideX + 1) on the left and
            // (x + kSampleStrideX - 1) on the right, similarly for y.
            local_ink_left   = std::max(0, local_ink_left - (kSampleStrideX - 1));
            local_ink_right  = std::min(sw - 1, local_ink_right + kSampleStrideX - 1);
            local_ink_top    = std::max(0, local_ink_top - (kSampleStrideY - 1));
            local_ink_bottom = std::min(sh - 1, local_ink_bottom + kSampleStrideY - 1);
            // Propagate row counts to skipped rows so trimming works correctly.
            // Only fill unsampled rows (y % kSampleStrideY != 0); sampled rows
            // that legitimately have zero count must stay zero.
            for (int y = 1; y < sh; ++y) {
                if (row_counts[static_cast<size_t>(y)] == 0 &&
                    y % kSampleStrideY != 0 &&
                    row_counts[static_cast<size_t>(y - 1)] > 0) {
                    row_counts[static_cast<size_t>(y)] =
                        row_counts[static_cast<size_t>(y - 1)];
                }
            }

            if (max_count > 0) {
                // 2. Find the last row that has *meaningful* content.
                const int content_threshold = std::max(5,
                    static_cast<int>(std::ceil(static_cast<float>(max_count) * 0.02f)));
                int last_content_row = -1;
                for (int y = 0; y < sh; ++y) {
                    if (row_counts[static_cast<size_t>(y)] > content_threshold) {
                        last_content_row = y;
                    }
                }

                // 3. Clear empty rows below the last ink.
                //    Use local_ink_bottom (absolute last row with any non-zero
                //    pixel) + a font-size-based safety margin for descender
                //    glyphs, instead of the 2%-threshold last_content_row which
                //    clips descenders (p, q, y, g, j) whose few bottom pixels
                //    fall below the threshold.
                //    When a text box is enabled, the box itself is the natural
                //    boundary — skip the trim entirely to be safe.
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

                    // 4. Also clear isolated haze rows at the very bottom.
                    const int trace_threshold = std::max(2,
                        static_cast<int>(std::ceil(static_cast<float>(max_count) * 0.005f)));
                    for (int y = sh - 1; y >= 0; --y) {
                        const int count = row_counts[static_cast<size_t>(y)];
                        if (count == 0) continue;
                        if (count > trace_threshold && y <= safe_bottom) break;
                        std::fill_n(pixels + y * stride, sw, uint32_t{0});
                    }
                }

                // 5. Compute ink fractional center for text-alignment fix.
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
            
            // Debug: log text bounds for box-enabled text (env-gated)
            if (t.box.enabled && !t.text.empty() && chronon3d::Config::get().debug_text_raster) {
                spdlog::info("[TextRaster] '{}' img={}x{} box={:.0f}x{:.0f} ink=[{},{},{},{}] ink_w={:.0f} ink_cx={:.1f}",
                    t.text, sw, sh, t.box.size.x, t.box.size.y,
                    ink_left, ink_right, ink_top, ink_bottom, ink_w, ink_center_frac);
            }
        }
    }

    // ── Debug: draw bounding box overlays (env-gated) ──────────────
    if (chronon3d::Config::get().debug_text_bbox && !t.text.empty()) {
        BLContext dbg(img);
        dbg.setCompOp(BL_COMP_OP_SRC_OVER);

        const float pad_f = static_cast<float>(padding) * 0.5f;

        // 1. Text box boundary (red) — only when box is enabled
        if (t.box.enabled) {
            dbg.setStrokeStyle(BLRgba32(255, 48, 48, 200));
            dbg.setStrokeWidth(2.0f);
            dbg.strokeRect(pad_f, pad_f, t.box.size.x, t.box.size.y);

            // Box center crosshair (cyan)
            const float cx = pad_f + t.box.size.x * 0.5f;
            const float cy = pad_f + t.box.size.y * 0.5f;
            dbg.setStrokeStyle(BLRgba32(0, 255, 255, 200));
            dbg.setStrokeWidth(1.0f);
            dbg.strokeLine(cx - 14.0f, cy, cx + 14.0f, cy);
            dbg.strokeLine(cx, cy - 14.0f, cx, cy + 14.0f);
        }

        // 2. Ink bounding box (green)
        if (ink_left < ink_right) {
            const float ink_x = static_cast<float>(ink_left);
            const float ink_y = static_cast<float>(ink_top);
            const float ink_w = static_cast<float>(ink_right - ink_left);
            const float ink_h = static_cast<float>(ink_bottom - ink_top);
            dbg.setStrokeStyle(BLRgba32(48, 255, 48, 200));
            dbg.setStrokeWidth(1.0f);
            dbg.strokeRect(ink_x, ink_y, ink_w, ink_h);

            // Ink center crosshair (yellow)
            const float icx = static_cast<float>(ink_left + ink_right) * 0.5f;
            const float icy = static_cast<float>(ink_top + ink_bottom) * 0.5f;
            dbg.setStrokeStyle(BLRgba32(255, 255, 48, 200));
            dbg.setStrokeWidth(1.0f);
            dbg.strokeLine(icx - 8.0f, icy, icx + 8.0f, icy);
            dbg.strokeLine(icx, icy - 8.0f, icx, icy + 8.0f);
        }

        // 3. Baseline (blue) — first line's expected baseline
        if (!layout_res.lines.empty()) {
            const float baseline_y = text_start_y + layout_res.lines[0].position.y + font.metrics().ascent;
            dbg.setStrokeStyle(BLRgba32(48, 128, 255, 180));
            dbg.setStrokeWidth(1.0f);
            dbg.strokeLine(0.0f, baseline_y, static_cast<float>(img_w), baseline_y);
        }

        // 4. Text label in top-left corner with content info
        if (!font.empty()) {
            // Use a fixed-size font for the debug label (10% of effective_size, min 12)
            BLFont dbg_label_font;
            const float label_size = std::max(12.0f, effective_size * 0.1f);
            dbg_label_font.createFromFace(face, label_size);
            if (!dbg_label_font.empty()) {
                char label_buf[128];
                std::snprintf(label_buf, sizeof(label_buf), "[%.*s] %.0fx%.0f",
                    std::min(24, static_cast<int>(t.text.size())), t.text.data(),
                    t.box.enabled ? t.box.size.x : 0.0f,
                    t.box.enabled ? t.box.size.y : 0.0f);
                dbg.setFillStyle(BLRgba32(255, 255, 255, 220));
                dbg.fillUtf8Text(BLPoint(4.0f, label_size + 2.0f), dbg_label_font, label_buf);
            }
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
            // Pixel-ink centering (opt-in via TextCenteringMode::PixelInk):
            // Adjust x_offset so that the actual ink centre aligns with the
            // centre of the text box.  This was the historical default when
            // the layout engine (FreeType+HarfBuzz) disagreed with Blend2D
            // rendering.  Now that both use Blend2D measurement, LayoutBox
            // is accurate enough and PixelInk is a debug/transition aid.
            if (t.style.centering_mode == TextCenteringMode::PixelInk &&
                ink_center_frac >= 0.0f && t.style.align == TextAlign::Center) {
                const float box_img_cx = static_cast<float>(img_w) * 0.5f;
                const float shift = std::round(box_img_cx - ink_center_frac);
                x_offset += shift;
            }
            // For Right-aligned text, shift image so box-right aligns with layer origin
            else if (t.style.align == TextAlign::Right) {
                x_offset -= t.box.size.x;
            }
        } else {
            const float full_width = layout_res.size.x;
            if (t.style.align == TextAlign::Center) x_offset = -full_width * 0.5f;
            else if (t.style.align == TextAlign::Right) x_offset = -full_width;
            x_offset += metrics.boundingBox.x0 - (padding / 2.0f);
            // Pixel-ink centering for non-box text (opt-in via PixelInk)
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

    // Debug: log final offsets for box text
    if (t.box.enabled && !t.text.empty() && chronon3d::Config::get().debug_text_raster) {
        spdlog::info("[TextFinal] '{}' box={:.0f}x{:.0f} img={}x{} ink_center={:.1f} shift={:.1f} x_offset_final={:.1f} y_offset_final={:.1f} model_x={:.0f} frame_ink_cx={:.0f}",
            t.text, t.box.size.x, t.box.size.y, img_w, img_h,
            ink_center_frac,
            (t.style.align == TextAlign::Center && ink_center_frac >= 0.0f ? static_cast<float>(img_w) * 0.5f - ink_center_frac : 0.0f),
            x_offset, y_offset,
            (transform ? (*transform)[3][0] : 0.0f),
            (transform ? (*transform)[3][0] + x_offset + (ink_center_frac >= 0.0f ? ink_center_frac : 0.0f) : 0.0f));
    }

    auto result = std::make_shared<TextRasterization>();
    result->image = std::move(img);
    result->x_offset = x_offset;
    result->y_offset = y_offset;
    result->metrics = metrics;
    result->font = font;

    store_text_cache(key, result);

    return *result;
}

} // namespace chronon3d
