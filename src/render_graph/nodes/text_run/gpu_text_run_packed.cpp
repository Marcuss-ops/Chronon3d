// SPDX-License-Identifier: MIT
//
// gpu_text_run_packed.cpp — packed-atlas and highlight helpers for the GPU
// text-run family.
//
// Split out of gpu_text_run.cpp: the overflow-checked multiply, the shelf
// packer, the fallback glyph rasterizer and the timed-highlight planner.
// Declarations live in gpu_text_run_detail.hpp so the surface-draw, MTSDF and
// cached TUs can share exactly one implementation.

#include "gpu_text_run_detail.hpp"

#include <chronon3d/backends/text/text_render_resources.hpp>
#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/text/animation/text_animator_properties.hpp>
#include <chronon3d/text/glyph_selector.hpp>
#include <chronon3d/text/glyph_selector_spec.hpp>

#include <blend2d.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace chronon3d::graph::text_run::detail {

namespace {

/// Fixed packed-atlas width.  Glyphs are shelf-packed left-to-right and
/// wrapped to a new row when a glyph would overflow this width.
constexpr std::uint32_t kMaxAtlasWidth = 1024;

} // namespace

/// Overflow-safe `a * b`.  Returns false on overflow and never writes `out`.
/// Glyph dimensions flow in from layout/font/effect code and must never be
/// trusted to fit `size_t` when computing payload sizes.
bool checked_mul(std::size_t a, std::size_t b, std::size_t& out) {
    if (a != 0 && b > std::numeric_limits<std::size_t>::max() / a) {
        return false;
    }
    out = a * b;
    return true;
}

std::optional<GlyphAtlasEntry> rasterize_missing_glyph(
    TextRenderResources& resources,
    const assets::AssetResolver& resolver,
    const std::string& font_path,
    std::uint32_t glyph_id,
    std::uint32_t font_size,
    const Color& color) {
    const auto handle = resources.resolve_handle(font_path,
        static_cast<float>(font_size), resolver);
    if (!handle.valid()) {
        spdlog::error("[rasterize_missing_glyph] handle not valid for font_path='{}'", font_path);
        return std::nullopt;
    }
    BLFont font;
    if (font.createFromFace(*handle.bl_face, static_cast<float>(font_size)) != BL_SUCCESS) {
        spdlog::error("[rasterize_missing_glyph] createFromFace failed for font_path='{}'", font_path);
        return std::nullopt;
    }
    BLBoxI bounds{};
    if (font.getGlyphBounds(&glyph_id, 0, &bounds, 1) != BL_SUCCESS) return std::nullopt;
    // Blend2D's BLBoxI result is already expressed in the font's pixel
    // coordinate space.  Dividing it by 64 (as if it were a 26.6 value)
    // produces a tiny bitmap while getGlyphOutlines() correctly returns the
    // full-size path.  Keep the two APIs in the same coordinate space.
    const double bx0 = static_cast<double>(bounds.x0);
    const double by0 = static_cast<double>(bounds.y0);
    const double bx1 = static_cast<double>(bounds.x1);
    const double by1 = static_cast<double>(bounds.y1);
    const int x0 = static_cast<int>(std::floor(bx0));
    const int y0 = static_cast<int>(std::floor(by0));
    const int width = static_cast<int>(std::ceil(bx1)) - x0;
    const int height = static_cast<int>(std::ceil(by1)) - y0;
    if (width <= 0 || height <= 0) return std::nullopt;

    auto image = std::make_shared<BLImage>(width, height, BL_FORMAT_PRGB32);
    BLContext context(*image);
    context.setCompOp(BL_COMP_OP_SRC_COPY);
    context.setFillStyle(BLRgba32(0, 0, 0, 0));
    context.fillAll();
    context.setCompOp(BL_COMP_OP_SRC_OVER);
    context.setFillStyle(BLRgba32(
        static_cast<std::uint32_t>(std::clamp(color.r, 0.0f, 1.0f) * 255.0f),
        static_cast<std::uint32_t>(std::clamp(color.g, 0.0f, 1.0f) * 255.0f),
        static_cast<std::uint32_t>(std::clamp(color.b, 0.0f, 1.0f) * 255.0f),
        static_cast<std::uint32_t>(std::clamp(color.a, 0.0f, 1.0f) * 255.0f)));
    // The compatibility BLGlyphRun prewarm can yield a dimensioned but empty
    // PRGB32 image for some resolved font faces. Blend2D can decode the same
    // glyph outline directly from its BLFont; draw that path into the bitmap
    // and keep the upload contract unchanged.
    BLPath outline;
    if (font.getGlyphOutlines(glyph_id, outline) == BL_SUCCESS &&
        !outline.empty()) {
        BLBox outline_box{};
        if (outline.getBoundingBox(&outline_box) == BL_SUCCESS &&
            outline.translate(BLPoint(-outline_box.x0, -outline_box.y0)) == BL_SUCCESS &&
            context.fillPath(outline) == BL_SUCCESS) {
            context.end();
            GlyphAtlasEntry entry;
            entry.image = std::move(image);
            entry.x_offset = x0;
            entry.y_offset = y0;
            entry.fill_color_rgba =
                (static_cast<std::uint32_t>(std::clamp(color.r, 0.0f, 1.0f) * 255.0f) << 24u) |
                (static_cast<std::uint32_t>(std::clamp(color.g, 0.0f, 1.0f) * 255.0f) << 16u) |
                (static_cast<std::uint32_t>(std::clamp(color.b, 0.0f, 1.0f) * 255.0f) << 8u) |
                static_cast<std::uint32_t>(std::clamp(color.a, 0.0f, 1.0f) * 255.0f);
            return entry;
        }
    }
    BLGlyphPlacement placement{};
    placement.placement.reset(0.0, 0.0);
    placement.advance.reset(0.0, 0.0);
    BLGlyphRun run{};
    run.glyphData = &glyph_id;
    run.glyphAdvance = static_cast<int8_t>(sizeof(glyph_id));
    run.placementData = &placement;
    run.placementAdvance = static_cast<int8_t>(sizeof(placement));
    run.placementType = BL_GLYPH_PLACEMENT_TYPE_ADVANCE_OFFSET;
    run.size = 1;
    context.fillGlyphRun(BLPoint(-x0, -y0), font, run);
    context.end();

    GlyphAtlasEntry entry;
    entry.image = std::move(image);
    entry.x_offset = x0;
    entry.y_offset = y0;
    entry.fill_color_rgba =
        (static_cast<std::uint32_t>(std::clamp(color.r, 0.0f, 1.0f) * 255.0f) << 24u) |
        (static_cast<std::uint32_t>(std::clamp(color.g, 0.0f, 1.0f) * 255.0f) << 16u) |
        (static_cast<std::uint32_t>(std::clamp(color.b, 0.0f, 1.0f) * 255.0f) << 8u) |
        static_cast<std::uint32_t>(std::clamp(color.a, 0.0f, 1.0f) * 255.0f);
    return entry;
}

TimedHighlightPlan build_timed_highlight_plan(const TextRunShape& shape) {
    TimedHighlightPlan plan;
    if (!shape.layout || shape.layout->units.word_count == 0) return plan;

    const auto glyph_count = shape.layout->placed.glyphs.size();
    if (shape.layout->units.glyph_to_word.size() < glyph_count) {
        spdlog::error(
            "[text-run] invalid glyph_to_word mapping: placed_glyphs={} glyph_to_word={}",
            glyph_count, shape.layout->units.glyph_to_word.size());
        return plan;
    }

    plan.glyph_ranges.assign(glyph_count, {-1.0f, -1.0f});
    const auto word_count = static_cast<float>(shape.layout->units.word_count);

    for (const auto& animator : shape.animators) {
        std::optional<Color> animator_color;
        for (const auto& property : animator.properties) {
            if (const auto* fill = std::get_if<FillColorProperty>(&property)) {
                animator_color = fill->color;
                break;
            }
        }
        const bool karaoke_named = animator.id.find("karaoke") != std::string::npos;
        // The GPU timed path is intentionally limited to karaoke animators.
        // Other word animators (for example active_word_pop) may also carry a
        // fill property but additionally change scale/background/stroke and
        // must keep the legacy per-glyph evaluation semantics.
        if (!karaoke_named) continue;

        for (const auto& selector : animator.selectors) {
            if (selector.unit != TextSelectorUnit::Word) continue;
            const auto bound = bind_selector_runtime(
                selector, shape.layout->units.word_count,
                SampleTime::from_frame(0.0, FrameRate{30, 1}));
            const auto& keyframes = selector.amount.keyframes();
            float start_frame = -1.0f;
            float end_frame = -1.0f;
            for (const auto& keyframe : keyframes) {
                if (start_frame < 0.0f && keyframe.value >= 99.0f) {
                    start_frame = static_cast<float>(keyframe.frame.integral());
                } else if (start_frame >= 0.0f && keyframe.value <= 1.0f) {
                    end_frame = static_cast<float>(keyframe.frame.integral());
                    break;
                }
            }
            if (start_frame < 0.0f || end_frame < start_frame) continue;

            const float first_word = std::clamp(
                bound.start * word_count / 100.0f,
                0.0f, word_count);
            const float last_word = std::clamp(
                bound.end * word_count / 100.0f,
                0.0f, word_count);
            for (std::size_t glyph = 0; glyph < plan.glyph_ranges.size(); ++glyph) {
                const auto word = static_cast<float>(
                    shape.layout->units.glyph_to_word[glyph]);
                if (word >= first_word && word < last_word &&
                    plan.glyph_ranges[glyph].first < 0.0f) {
                    plan.glyph_ranges[glyph] = {start_frame, end_frame};
                    plan.enabled = true;
                }
            }
        }
        if (animator_color) plan.color = *animator_color;
    }
    return plan;
}

/// Shelf-pack the glyph quads.  Returns the per-glyph atlas origins (in the
/// same order as `glyphs`) plus the resulting atlas dimensions (at least 1x1).
std::pair<std::vector<PackedGlyph>, AtlasDims> pack_glyphs(
    std::span<const GpuTextGlyph> glyphs) {
    std::vector<PackedGlyph> packed;
    packed.reserve(glyphs.size());

    std::uint32_t cur_x = 0;
    std::uint32_t cur_y = 0;
    std::uint32_t row_height = 0;
    std::uint32_t atlas_width = 1;

    for (const auto& glyph : glyphs) {
        const std::uint32_t w = std::max(1u, glyph.width);
        const std::uint32_t h = std::max(1u, glyph.height);
        if (cur_x + w > kMaxAtlasWidth && cur_x > 0) {
            cur_x = 0;
            cur_y += row_height;
            row_height = 0;
        }
        packed.push_back(PackedGlyph{cur_x, cur_y});
        cur_x += w;
        row_height = std::max(row_height, h);
        atlas_width = std::max(atlas_width, cur_x);
    }
    const std::uint32_t atlas_height = std::max(1u, cur_y + row_height);
    return {std::move(packed), AtlasDims{atlas_width, atlas_height}};
}

} // namespace chronon3d::graph::text_run::detail
