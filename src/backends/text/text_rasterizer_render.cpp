#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <cctype>
#include <mutex>
#include <unordered_map>
#include <optional>
#include <vector>

#include <chronon3d/assets/asset_registry.hpp>
#include <chronon3d/backends/text/text_rasterizer_utils.hpp>
#include <chronon3d/backends/text/text_layout_engine.hpp>
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

std::optional<TextRasterization> rasterize_text_to_bl_image(
    const TextShape& t,
    float effective_size,
    int padding,
    bool* cache_hit,
    const Mat4* transform
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

    TextLayoutInput layout_in;
    layout_in.text = t.text;
    layout_in.style = t.style;
    layout_in.style.size = effective_size;
    layout_in.box = layout_box;
    layout_in.char_width_ctx = &font;
    layout_in.char_width_fn = [](const void* ctx, char c, float font_size) -> float {
        auto& f = *static_cast<const BLFont*>(ctx);
        BLGlyphBuffer gb;
        char buf[2] = {c, '\0'};
        gb.setUtf8Text(buf, 1);
        f.shape(gb);
        BLTextMetrics m;
        f.getTextMetrics(gb, m);
        float base_w = static_cast<float>(m.advance.x);
        return base_w * (font_size / f.size());
    };

    auto start_layout = std::chrono::steady_clock::now();
    auto layout_res = TextLayoutEngine::layout(layout_in);
    auto end_layout = std::chrono::steady_clock::now();
    if (profiling::g_current_counters) {
        auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(end_layout - start_layout).count();
        profiling::g_current_counters->text_layout_ms.fetch_add(static_cast<uint64_t>(dur), std::memory_order_relaxed);
    }

    // Recreate the font at the final resolved size from the layout engine
    font.createFromFace(face, layout_res.font_size);

    int tw = 0;
    int th = 0;
    if (t.box.enabled) {
        tw = static_cast<int>(std::ceil(t.box.size.x)) + padding;
        th = static_cast<int>(std::ceil(t.box.size.y)) + padding;
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

    auto start_raster = std::chrono::steady_clock::now();

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
    if (t.box.enabled && free_w > layout_res.size.x) {
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
    float text_start_y = padding / 2.0f + (t.style.box_style.enabled ? t.style.box_style.padding.y : 0.0f) + dy_align;

    Color fill_color = t.style.paint.fill;
    if (fill_color == Color{1.0f, 1.0f, 1.0f, 1.0f} && !(t.style.color == Color{1.0f, 1.0f, 1.0f, 1.0f})) {
        fill_color = t.style.color;
    }

    const float text_block_w = std::max(1.0f, layout_res.size.x);
    const float text_block_h = std::max(1.0f, layout_res.size.y);

    for (const auto& line : layout_res.lines) {
        if (line.text.empty()) continue;

        float lx = text_start_x + line.position.x;
        float ly = text_start_y + line.position.y + font.metrics().ascent;

        if (t.style.paint.stroke_enabled && t.style.paint.stroke_width > 0.0f) {
            ctx.setStrokeWidth(t.style.paint.stroke_width);
            ctx.setStrokeStyle(to_bl_rgba(t.style.paint.stroke_color));
            ctx.strokeUtf8Text(BLPoint(lx, ly), font, line.text.c_str());
        }

        apply_text_fill_style(
            ctx,
            t.style,
            fill_color,
            text_start_x,
            text_start_y,
            text_block_w,
            text_block_h
        );
        ctx.fillUtf8Text(BLPoint(lx, ly), font, line.text.c_str());
    }

    ctx.end();

    // Trim trailing rows from the raster that contain only sparse / trace
    // alpha residue.  This removes dark-band artefacts that would otherwise
    // appear below the text when composited over a flat background (caused by
    // font hinting, sub-pixel glow, or Blend2D bounding-box edge effects).
    {
        BLImageData trim_data;
        if (img.getData(&trim_data) == BL_SUCCESS && trim_data.pixelData && trim_data.size.h > 0) {
            const int sw = trim_data.size.w;
            const int sh = trim_data.size.h;
            const int stride = static_cast<int>(trim_data.stride / sizeof(uint32_t));
            auto* pixels = reinterpret_cast<uint32_t*>(trim_data.pixelData);

            // 1. Count non-zero-alpha pixels per row and find the maximum.
            std::vector<int> row_counts(static_cast<size_t>(sh), 0);
            int max_count = 0;
            for (int y = 0; y < sh; ++y) {
                int count = 0;
                for (int x = 0; x < sw; ++x) {
                    if (((pixels[y * stride + x] >> 24) & 0xFF) != 0) {
                        ++count;
                    }
                }
                row_counts[static_cast<size_t>(y)] = count;
                max_count = std::max(max_count, count);
            }

            if (max_count > 0) {
                // 2. Find the last row that has *meaningful* content (≥ 2 % of
                //    the densest row).  Everything strictly below that is haze.
                const int content_threshold = std::max(5,
                    static_cast<int>(std::ceil(static_cast<float>(max_count) * 0.02f)));
                int last_content_row = -1;
                for (int y = 0; y < sh; ++y) {
                    if (row_counts[static_cast<size_t>(y)] > content_threshold) {
                        last_content_row = y;
                    }
                }

                // 3. Clear everything below last_content_row (inclusive guard).
                if (last_content_row >= 0 && last_content_row < sh - 1) {
                    for (int y = last_content_row + 1; y < sh; ++y) {
                        std::fill_n(pixels + y * stride, sw, uint32_t{0});
                    }
                }

                // 4. Also clear any isolated haze rows at the very bottom
                //    (below the first fully-empty gap from the bottom) that have
                //    only trace coverage (< 0.5 % of max).
                const int trace_threshold = std::max(2,
                    static_cast<int>(std::ceil(static_cast<float>(max_count) * 0.005f)));
                for (int y = sh - 1; y >= 0; --y) {
                    const int count = row_counts[static_cast<size_t>(y)];
                    if (count == 0) continue;               // already empty, keep going up
                    if (count > trace_threshold) break;     // meaningful content → stop
                    std::fill_n(pixels + y * stride, sw, uint32_t{0});
                }
            }
        }
    }

    auto end_raster = std::chrono::steady_clock::now();
    if (profiling::g_current_counters) {
        auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(end_raster - start_raster).count();
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
        } else {
            const float full_width = layout_res.size.x;
            if (t.style.align == TextAlign::Center) x_offset = -full_width * 0.5f;
            else if (t.style.align == TextAlign::Right) x_offset = -full_width;
            x_offset += metrics.boundingBox.x0 - (padding / 2.0f);
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
}

} // namespace chronon3d
