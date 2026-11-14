// Native GPU vector-stroke text coverage preparation.
// SPDX-License-Identifier: MIT
//
// Vector strokes are expanded outlines, not distance-field dilations.  This TU
// builds a raster-size-specific CoverageRgba atlas entry per glyph:
//
//   R,B = fill coverage (duplicated so the shader is independent of whether
//         the Vulkan page is RGBA8 or BGRA8; G is stable in both formats)
//   G   = stroke coverage outside the fill
//   A   = 1
//
// The shader applies the animated fill/stroke colours, so colour animation does
// not invalidate the atlas.  The glyph key carries the stroke geometry (width,
// join, cap and miter limit) plus the font size, which is what makes the entry
// distinct from the MTSDF entry for the same glyph.

#include "gpu_text_run_detail.hpp"

#include <chronon3d/runtime/gpu_glyph_atlas.hpp>
#include <chronon3d/runtime/gpu_text_selector_metadata.hpp>
#include <chronon3d/runtime/render_surface.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/text/text_run_shape.hpp>
#include <chronon3d/core/profiling/profiling.hpp>

#include <clipper2/clipper.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace chronon3d::graph::text_run {
namespace {

constexpr double kContourScale = 1024.0;
constexpr std::uint32_t kMaxAtlasGlyphPx = 2048;

struct Bitmap {
    std::uint32_t width{0};
    std::uint32_t height{0};
    float x_offset{0.0f};
    float y_offset{0.0f};
    std::vector<float> pixels;
};

struct Edge {
    double x0{0.0};
    double y0{0.0};
    double x1{0.0};
    double y1{0.0};
};

struct Crossing {
    double x{0.0};
    int direction{0};
};

Clipper2Lib::JoinType clipper_join(LineJoin join) noexcept {
    switch (join) {
        case LineJoin::Round: return Clipper2Lib::JoinType::Round;
        case LineJoin::Bevel: return Clipper2Lib::JoinType::Bevel;
        case LineJoin::Miter:
        default: return Clipper2Lib::JoinType::Miter;
    }
}

bool finite_contours(const std::vector<std::vector<Vec2>>& contours) noexcept {
    for (const auto& contour : contours) {
        for (const auto& point : contour) {
            if (!std::isfinite(point.x) || !std::isfinite(point.y)) return false;
        }
    }
    return true;
}

Clipper2Lib::Paths64 to_clipper_paths(
    const std::vector<std::vector<Vec2>>& contours) {
    Clipper2Lib::Paths64 paths;
    paths.reserve(contours.size());
    for (const auto& contour : contours) {
        if (contour.size() < 3) continue;
        Clipper2Lib::Path64 path;
        path.reserve(contour.size());
        for (const auto& point : contour) {
            path.emplace_back(
                static_cast<std::int64_t>(std::llround(
                    static_cast<double>(point.x) * kContourScale)),
                static_cast<std::int64_t>(std::llround(
                    static_cast<double>(point.y) * kContourScale)));
        }
        if (path.size() >= 3) paths.push_back(std::move(path));
    }
    return paths;
}

void add_span(std::vector<float>& row, double x0, double x1,
              double left, std::uint32_t width) {
    if (!(x1 > x0)) return;
    x0 = std::max(x0, left);
    x1 = std::min(x1, left + static_cast<double>(width));
    if (!(x1 > x0)) return;
    const auto first = static_cast<std::int64_t>(std::floor(x0 - left));
    const auto last = static_cast<std::int64_t>(std::ceil(x1 - left));
    const auto begin = std::clamp<std::int64_t>(first, 0, width);
    const auto end = std::clamp<std::int64_t>(last, 0, width);
    for (auto px = begin; px < end; ++px) {
        const double px0 = left + static_cast<double>(px);
        const double px1 = px0 + 1.0;
        const double overlap = std::min(x1, px1) - std::max(x0, px0);
        if (overlap > 0.0) {
            row[static_cast<std::size_t>(px)] +=
                static_cast<float>(overlap);
        }
    }
}

// Non-zero scanline rasterizer with four vertical sub-samples per pixel and
// exact horizontal span coverage.  Glyph outlines are closed contours, so no
// cap handling is needed here; Clipper2 has already applied the join style to
// the expanded outline.
std::vector<float> rasterize_paths(
    const Clipper2Lib::Paths64& paths,
    double left, double top,
    std::uint32_t width, std::uint32_t height) {
    std::vector<float> mask(
        static_cast<std::size_t>(width) * height, 0.0f);
    if (width == 0 || height == 0 || paths.empty()) return mask;

    std::vector<Edge> edges;
    for (const auto& path : paths) {
        if (path.size() < 3) continue;
        for (std::size_t i = 0; i < path.size(); ++i) {
            const auto& a = path[i];
            const auto& b = path[(i + 1) % path.size()];
            edges.push_back(Edge{
                static_cast<double>(a.x) / kContourScale,
                static_cast<double>(a.y) / kContourScale,
                static_cast<double>(b.x) / kContourScale,
                static_cast<double>(b.y) / kContourScale});
        }
    }
    if (edges.empty()) return mask;

    constexpr int kSubSamples = 4;
    std::vector<float> row(width, 0.0f);
    std::vector<Crossing> crossings;
    crossings.reserve(edges.size());
    for (std::uint32_t py = 0; py < height; ++py) {
        std::fill(row.begin(), row.end(), 0.0f);
        for (int sub = 0; sub < kSubSamples; ++sub) {
            const double y = top + static_cast<double>(py) +
                (static_cast<double>(sub) + 0.5) /
                    static_cast<double>(kSubSamples);
            crossings.clear();
            for (const auto& edge : edges) {
                const double y_min = std::min(edge.y0, edge.y1);
                const double y_max = std::max(edge.y0, edge.y1);
                if (y < y_min || y >= y_max) continue;
                const double dy = edge.y1 - edge.y0;
                if (std::abs(dy) < 1e-12) continue;
                const double x = edge.x0 +
                    (y - edge.y0) * (edge.x1 - edge.x0) / dy;
                crossings.push_back(Crossing{
                    x, edge.y1 > edge.y0 ? 1 : -1});
            }
            if (crossings.size() < 2) continue;
            std::sort(crossings.begin(), crossings.end(),
                [](const Crossing& a, const Crossing& b) {
                    return a.x < b.x;
                });
            int winding = 0;
            for (std::size_t i = 0; i < crossings.size(); ++i) {
                winding += crossings[i].direction;
                if (winding != 0 && i + 1 < crossings.size()) {
                    add_span(row, crossings[i].x, crossings[i + 1].x,
                             left, width);
                }
            }
        }
        const float inv_subsamples = 1.0f /
            static_cast<float>(kSubSamples);
        const std::size_t dst = static_cast<std::size_t>(py) * width;
        for (std::uint32_t px = 0; px < width; ++px) {
            mask[dst + px] = std::clamp(
                row[px] * inv_subsamples, 0.0f, 1.0f);
        }
    }
    return mask;
}

bool load_outline(
    const FontEngine& engine,
    const ShapedFontSpan& span,
    std::uint32_t glyph_id,
    float font_size,
    std::vector<std::vector<Vec2>>& outlines) {
    const FontSpec font_spec{
        span.font.font_path, span.font.font_family,
        span.font.font_weight, span.font.font_style, font_size};
    outlines = engine.glyph_outline_contours(
        font_spec, glyph_id, font_size, span.variation_axes);
    return !outlines.empty() && finite_contours(outlines);
}

std::string vector_key_axes(
    const ShapedFontSpan& span,
    float stroke_width,
    LineCap cap,
    LineJoin join,
    float miter_limit) {
    std::string axes = span.variation_axes;
    axes += "|vstroke:";
    axes += std::to_string(static_cast<long long>(std::llround(
        static_cast<double>(stroke_width) * 1024.0)));
    axes += ':';
    axes += std::to_string(static_cast<unsigned>(cap));
    axes += ':';
    axes += std::to_string(static_cast<unsigned>(join));
    axes += ':';
    axes += std::to_string(static_cast<long long>(std::llround(
        static_cast<double>(miter_limit) * 1024.0)));
    return axes;
}

std::uint32_t unit_index_for_glyph(
    const TextRunShape& shape, std::size_t layout_index) {
    const auto& layout = *shape.layout;
    if (shape.animators.empty() || shape.animators.front().selectors.empty())
        return std::numeric_limits<std::uint32_t>::max();
    const auto selector_unit = shape.animators.front().selectors.front().unit;
    if (selector_unit == TextSelectorUnit::Word &&
        layout_index < layout.units.glyph_to_word.size()) {
        return layout.units.glyph_to_word[layout_index];
    }
    if (selector_unit == TextSelectorUnit::Line &&
        layout_index < layout.units.glyph_to_line.size()) {
        return layout.units.glyph_to_line[layout_index];
    }
    if ((selector_unit == TextSelectorUnit::Grapheme ||
         selector_unit == TextSelectorUnit::Character) &&
        layout_index < layout.units.glyph_to_grapheme.size()) {
        return layout.units.glyph_to_grapheme[layout_index];
    }
    return std::numeric_limits<std::uint32_t>::max();
}

} // namespace

std::shared_ptr<const runtime::GpuPreparedTextRun>
prepare_vector_stroke_text_run(
    runtime::GpuGlyphAtlas& glyph_atlas,
    const TextRunShape& shape) {
    if (!shape.layout || !shape.engine || shape.animated_doc ||
        shape.layout->font_spans.empty() || shape.paint.fill_style ||
        shape.paint.fill_pattern || shape.paint.stroke_style ||
        shape.paint.stroke_pattern ||
        (shape.material.enabled && !shape.material.gel_surface) ||
        !shape.paint.stroke_dash_array.empty() ||
        shape.paint.stroke_dash_offset != 0.0f ||
        shape.glyphs.size() != shape.layout->placed.glyphs.size()) {
        return nullptr;
    }
    if (shape.paint.stroke_enabled &&
        (!std::isfinite(shape.paint.stroke_width) ||
         shape.paint.stroke_width < 0.0f ||
         shape.paint.stroke_width > 64.0f)) {
        return nullptr;
    }
    if (!std::isfinite(shape.paint.stroke_miter_limit) ||
        shape.paint.stroke_miter_limit <= 0.0f) {
        return nullptr;
    }

    const auto& layout = *shape.layout;
    std::uint32_t covered_glyphs = 0;
    for (const auto& span : layout.font_spans) {
        if (span.glyph_begin != covered_glyphs ||
            span.glyph_end < span.glyph_begin ||
            span.glyph_end > layout.placed.glyphs.size()) {
            return nullptr;
        }
        covered_glyphs = span.glyph_end;
    }
    if (covered_glyphs != layout.placed.glyphs.size()) return nullptr;

    auto prepared = std::make_shared<runtime::GpuPreparedTextRun>();
    prepared->atlas_generation = glyph_atlas.generation();

    const runtime::DistanceFieldProfile profile{
        runtime::GlyphRepresentation::CoverageRgba, 0, 0, 0};

    std::vector<runtime::GpuGlyphKey> keys;
    std::vector<std::size_t> drawable_layout;
    std::vector<std::uint32_t> widths;
    std::vector<std::uint32_t> heights;
    std::vector<runtime::GpuGlyphMetrics> metrics;
    std::vector<runtime::GpuGlyphKey> pending_keys;
    std::vector<std::uint32_t> pending_widths;
    std::vector<std::uint32_t> pending_heights;
    std::vector<runtime::GpuGlyphMetrics> pending_metrics;
    std::vector<float> pending_pixels;

    std::size_t span_index = 0;
    for (std::size_t i = 0; i < layout.placed.glyphs.size(); ++i) {
        while (span_index + 1 < layout.font_spans.size() &&
               i >= layout.font_spans[span_index].glyph_end) {
            ++span_index;
        }
        const auto& state = shape.glyphs[i];
        const auto& placed = layout.placed.glyphs[i];
        if (state.glyph_id == 0 || placed.bbox_x1 <= placed.bbox_x0) continue;
        const auto& span = layout.font_spans[span_index];
        const float font_size = span.font_size > 0.0f
            ? span.font_size : layout.font_size;
        if (!std::isfinite(font_size) || font_size <= 0.0f ||
            font_size > 4096.0f) {
            return nullptr;
        }
        const bool glyph_stroke =
            shape.paint.stroke_enabled && state.stroke.a > 0.0f &&
            state.stroke_width > 0.0f;
        const float stroke_width = glyph_stroke
            ? std::min(state.stroke_width, 64.0f)
            : (shape.paint.stroke_enabled
                ? std::min(shape.paint.stroke_width, 64.0f) : 0.0f);
        const auto font_size_px = static_cast<std::uint32_t>(
            std::clamp(std::llround(static_cast<double>(font_size)),
                       1LL, 4096LL));
        const auto key = runtime::GpuGlyphKey::make(
            span.font.font_path, state.glyph_id,
            vector_key_axes(span, stroke_width, shape.paint.stroke_cap,
                            shape.paint.stroke_join,
                            shape.paint.stroke_miter_limit),
            runtime::GlyphRepresentation::CoverageRgba, 0, font_size_px);
        keys.push_back(key);
        drawable_layout.push_back(i);

        std::array<std::optional<runtime::GpuGlyphAtlas::Residency>, 1>
            resident_slot{};
        const std::array<runtime::GpuGlyphKey, 1> key_slot{key};
        glyph_atlas.resolve_resident(key_slot, resident_slot);
        if (resident_slot[0].has_value()) {
            if (profiling::g_current_counters) {
                profiling::g_current_counters->glyph_cache_hits.fetch_add(
                    1, std::memory_order_relaxed);
            }
            const auto& resident = *resident_slot[0];
            widths.push_back(resident.location.width);
            heights.push_back(resident.location.height);
            metrics.push_back(resident.metrics.value_or(
                runtime::GpuGlyphMetrics{0.0f, 0.0f, placed.advance_x}));
            continue;
        }
        if (profiling::g_current_counters) {
            profiling::g_current_counters->glyph_cache_misses.fetch_add(
                1, std::memory_order_relaxed);
        }

        std::vector<std::vector<Vec2>> outlines;
        if (!load_outline(*shape.engine, span, state.glyph_id, font_size,
                          outlines)) {
            return nullptr;
        }
        auto fill_paths = to_clipper_paths(outlines);
        if (fill_paths.empty()) return nullptr;
        fill_paths = Clipper2Lib::Union(
            fill_paths, Clipper2Lib::FillRule::NonZero);
        if (fill_paths.empty()) return nullptr;

        Clipper2Lib::Paths64 stroke_paths;
        if (stroke_width > 0.0f) {
            stroke_paths = Clipper2Lib::InflatePaths(
                fill_paths, static_cast<double>(stroke_width) * 0.5 *
                    kContourScale,
                clipper_join(shape.paint.stroke_join),
                Clipper2Lib::EndType::Polygon,
                static_cast<double>(shape.paint.stroke_miter_limit),
                0.25);
            if (stroke_paths.empty()) return nullptr;
        }

        const auto& bounds_paths =
            stroke_paths.empty() ? fill_paths : stroke_paths;
        double min_x = std::numeric_limits<double>::infinity();
        double min_y = std::numeric_limits<double>::infinity();
        double max_x = -std::numeric_limits<double>::infinity();
        double max_y = -std::numeric_limits<double>::infinity();
        for (const auto& path : bounds_paths) {
            for (const auto& point : path) {
                const double x = static_cast<double>(point.x) / kContourScale;
                const double y = static_cast<double>(point.y) / kContourScale;
                min_x = std::min(min_x, x);
                min_y = std::min(min_y, y);
                max_x = std::max(max_x, x);
                max_y = std::max(max_y, y);
            }
        }
        if (!std::isfinite(min_x) || !std::isfinite(min_y) ||
            !std::isfinite(max_x) || !std::isfinite(max_y) ||
            max_x <= min_x || max_y <= min_y) {
            return nullptr;
        }
        const double padding = 2.0 + static_cast<double>(stroke_width) * 0.5;
        const double left = std::floor(min_x - padding);
        const double top = std::floor(min_y - padding);
        const double right = std::ceil(max_x + padding);
        const double bottom = std::ceil(max_y + padding);
        if (!(right > left) || !(bottom > top)) return nullptr;
        const double width_d = right - left;
        const double height_d = bottom - top;
        if (width_d > static_cast<double>(kMaxAtlasGlyphPx) ||
            height_d > static_cast<double>(kMaxAtlasGlyphPx)) {
            return nullptr;
        }
        const auto width = static_cast<std::uint32_t>(width_d);
        const auto height = static_cast<std::uint32_t>(height_d);
        if (width == 0 || height == 0) return nullptr;

        auto fill_mask = rasterize_paths(
            fill_paths, left, top, width, height);
        std::vector<float> stroke_mask;
        if (!stroke_paths.empty()) {
            stroke_mask = rasterize_paths(
                stroke_paths, left, top, width, height);
        }

        Bitmap bitmap;
        bitmap.width = width;
        bitmap.height = height;
        bitmap.x_offset = static_cast<float>(left);
        bitmap.y_offset = static_cast<float>(top);
        bitmap.pixels.resize(
            static_cast<std::size_t>(width) * height, 0.0f);
        for (std::size_t p = 0; p < bitmap.pixels.size(); ++p) {
            const float fill = std::clamp(fill_mask[p], 0.0f, 1.0f);
            const float stroke = stroke_mask.empty()
                ? 0.0f
                : std::clamp(stroke_mask[p] - fill, 0.0f, 1.0f);
            const auto r = static_cast<std::uint32_t>(
                std::lround(fill * 255.0f));
            const auto g = static_cast<std::uint32_t>(
                std::lround(stroke * 255.0f));
            const std::uint32_t packed =
                (r & 0xFFu) | ((g & 0xFFu) << 8u) |
                ((r & 0xFFu) << 16u) | (255u << 24u);
            bitmap.pixels[p] = std::bit_cast<float>(packed);
        }

        const runtime::GpuGlyphMetrics glyph_metrics{
            bitmap.x_offset, bitmap.y_offset, placed.advance_x};
        widths.push_back(bitmap.width);
        heights.push_back(bitmap.height);
        metrics.push_back(glyph_metrics);
        pending_keys.push_back(key);
        pending_widths.push_back(bitmap.width);
        pending_heights.push_back(bitmap.height);
        pending_metrics.push_back(glyph_metrics);
        pending_pixels.insert(pending_pixels.end(),
                              bitmap.pixels.begin(), bitmap.pixels.end());
    }

    if (!pending_keys.empty()) {
        const auto upload_t0 = profiling::g_current_counters
            ? profiling::now() : profiling::Clock::time_point{};
        const auto uploaded = glyph_atlas.prepare_batch(
            pending_keys, pending_widths, pending_heights, pending_pixels,
            pending_metrics, profile);
        if (profiling::g_current_counters) {
            profiling::g_current_counters->glyph_atlas_upload_wall_us.fetch_add(
                static_cast<std::uint64_t>(std::llround(
                    profiling::elapsed_us(upload_t0))),
                std::memory_order_relaxed);
        }
        if (!uploaded.has_value()) return nullptr;
    }

    std::vector<std::optional<runtime::GpuGlyphAtlas::Residency>>
        residency(keys.size());
    glyph_atlas.resolve_resident(keys, residency);
    for (std::size_t key_index = 0; key_index < keys.size(); ++key_index) {
        const auto& resident = residency[key_index];
        if (!resident) continue;
        const auto page = resident->page;
        if (page == runtime::kInvalidRenderSurfaceHandle) continue;
        auto batch = std::find_if(
            prepared->batches.begin(), prepared->batches.end(),
            [page](const runtime::GpuPreparedTextRun::Batch& candidate) {
                return candidate.page == page;
            });
        if (batch == prepared->batches.end()) {
            prepared->batches.push_back(runtime::GpuPreparedTextRun::Batch{});
            batch = std::prev(prepared->batches.end());
            batch->page = page;
            batch->glyphs.reserve(keys.size());
        }
        const std::size_t layout_index = drawable_layout[key_index];
        const auto& placed = layout.placed.glyphs[layout_index];
        const auto& location = resident->location;
        runtime::GlyphStatic glyph{};
        glyph.atlas_page = 0;
        glyph.flags = static_cast<std::uint16_t>(
            runtime::pack_mtsdf_glyph_unit_flags(
                0u, unit_index_for_glyph(shape, layout_index)));
        glyph.atlas_x = location.atlas_x;
        glyph.atlas_y = location.atlas_y;
        glyph.atlas_w = location.width;
        glyph.atlas_h = location.height;
        // Coverage is rasterised at the layout font size, so the atlas plane
        // is already in layout pixels and must not be rescaled by an MTSDF
        // em-to-layout factor.
        glyph.plane_left = placed.x + location.plane_left;
        glyph.plane_top = placed.y + location.plane_top;
        glyph.plane_right = glyph.plane_left +
            static_cast<float>(location.width);
        glyph.plane_bottom = glyph.plane_top +
            static_cast<float>(location.height);
        glyph.draw_order = static_cast<std::uint32_t>(layout_index);
        glyph.pad0 = static_cast<std::uint32_t>(layout_index);
        glyph.origin_x = placed.x;
        glyph.origin_y = placed.y;
        batch->glyphs.push_back(glyph);
    }
    prepared->ready = !prepared->batches.empty() &&
        std::any_of(prepared->batches.begin(), prepared->batches.end(),
                    [](const runtime::GpuPreparedTextRun::Batch& batch) {
                        return !batch.glyphs.empty() &&
                            batch.page != runtime::kInvalidRenderSurfaceHandle;
                    });
    return prepared;
}

} // namespace chronon3d::graph::text_run
