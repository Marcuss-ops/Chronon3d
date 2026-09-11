#include "text_texture_cache.hpp"

#ifdef CHRONON3D_USE_BLEND2D
#include <blend2d.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace chronon3d::media::video {
namespace {

std::string require_prepared_font(const std::string& requested) {
    if (requested.empty()) {
        throw std::runtime_error("text layer has no prepared font asset");
    }
    if (!std::filesystem::exists(requested)) {
        throw std::runtime_error("prepared font asset not found: " + requested);
    }
    return requested;
}

} // namespace

RasterizedTextTexture rasterize_text_texture(
    const TextRunShape& prepared_shape,
    const Color& fill_color,
    const Color& stroke_color,
    float stroke_width,
    bool has_background,
    const Color& bg_color,
    float bg_opacity,
    float bg_radius,
    float pad_x,
    float pad_y,
    float box_w,
    float box_h,
    const std::string& alignment) {
#ifndef CHRONON3D_USE_BLEND2D
    (void)prepared_shape; (void)fill_color;
    (void)stroke_color; (void)stroke_width; (void)has_background; (void)bg_color;
    (void)bg_opacity; (void)bg_radius; (void)pad_x; (void)pad_y; (void)box_w;
    (void)box_h; (void)alignment;
    throw std::runtime_error("Blend2D is required for direct text rasterization");
#else
    if (!prepared_shape.layout) {
        throw std::runtime_error("text layer has no prepared TextRunLayout");
    }
    const auto& layout = *prepared_shape.layout;
    const float effective_font_size = layout.font_size > 0.0f
        ? layout.font_size : 58.0f;
    // TextRunLayout is the single shaping authority.  Its positions already
    // include HarfBuzz cluster resolution and tracking; DirectYUV only lowers
    // those immutable glyphs into a temporary CPU raster for the RGBA image
    // resource.  It must not call BLFont::shape() a second time.
    float min_x = 0.0f;
    float min_y = 0.0f;
    float max_x = std::max(layout.bounds.x, layout.placed.total_width);
    float max_y = std::max(layout.bounds.y, layout.placed.total_height);
    struct SpanRaster {
        BLFontFace face;
        BLFont font;
        std::vector<std::uint32_t> glyph_ids;
        std::vector<BLPoint> origins;
    };
    std::vector<std::unique_ptr<SpanRaster>> spans;
    spans.reserve(layout.font_spans.size());
    for (const auto& span : layout.font_spans) {
        if (span.glyph_begin > span.glyph_end ||
            span.glyph_end > layout.placed.glyphs.size()) {
            throw std::runtime_error("prepared text font span is out of bounds");
        }
        auto raster = std::make_unique<SpanRaster>();
        const std::string font_file = require_prepared_font(span.font.font_path);
        const BLResult face_res = raster->face.createFromFile(font_file.c_str());
        if (face_res != BL_SUCCESS) {
            throw std::runtime_error("prepared font asset could not be loaded: " + font_file);
        }
        raster->font.createFromFace(raster->face, effective_font_size);
        raster->glyph_ids.reserve(span.glyph_end - span.glyph_begin);
        raster->origins.reserve(span.glyph_end - span.glyph_begin);
        for (std::size_t index = span.glyph_begin; index < span.glyph_end; ++index) {
            const auto& glyph = layout.placed.glyphs[index];
            if (glyph.glyph_id == 0) continue;
            raster->glyph_ids.push_back(glyph.glyph_id);
            min_x = std::min(min_x, glyph.x + glyph.bbox_x0);
            min_y = std::min(min_y, glyph.y + glyph.bbox_y0);
            max_x = std::max(max_x, glyph.x + glyph.bbox_x1);
            max_y = std::max(max_y, glyph.y + glyph.bbox_y1);
            BLPoint origin{};
            origin.reset(glyph.x, glyph.y);
            raster->origins.push_back(origin);
        }
        spans.push_back(std::move(raster));
    }
    const float content_w = std::max(1.0f, max_x - min_x);
    const float content_h = std::max(1.0f, max_y - min_y);

    float target_w = content_w;
    float target_h = content_h;
    if (has_background) {
        target_w = content_w + pad_x * 2.0f;
        target_h = content_h + pad_y * 2.0f;
        target_w = std::max(target_w, box_w);
        target_h = std::max(target_h, box_h);
    } else {
        target_w += stroke_width * 2.0f + 24.0f;
        target_h += stroke_width * 2.0f + 16.0f;
    }

    int img_w = (static_cast<int>(std::ceil(target_w)) + 3) & ~3;
    int img_h = (static_cast<int>(std::ceil(target_h)) + 1) & ~1;
    img_w = std::max(img_w, 16);
    img_h = std::max(img_h, 16);

    BLImage bl_img(img_w, img_h, BL_FORMAT_PRGB32);
    BLContext ctx(bl_img);
    ctx.setCompOp(BL_COMP_OP_SRC_COPY);
    ctx.setFillStyle(BLRgba32(0, 0, 0, 0));
    ctx.fillAll();
    ctx.setCompOp(BL_COMP_OP_SRC_OVER);

    if (has_background) {
        const float card_w = std::max(content_w + pad_x * 2.0f, box_w);
        const float card_h = std::max(content_h + pad_y * 2.0f, box_h);
        const float card_x = (static_cast<float>(img_w) - card_w) * 0.5f;
        const float card_y = (static_cast<float>(img_h) - card_h) * 0.5f;
        const uint32_t a = static_cast<uint32_t>(std::clamp(bg_opacity * bg_color.a, 0.0f, 1.0f) * 255.0f);
        const uint32_t r = static_cast<uint32_t>(std::clamp(bg_color.r, 0.0f, 1.0f) * 255.0f);
        const uint32_t g = static_cast<uint32_t>(std::clamp(bg_color.g, 0.0f, 1.0f) * 255.0f);
        const uint32_t b = static_cast<uint32_t>(std::clamp(bg_color.b, 0.0f, 1.0f) * 255.0f);
        ctx.setFillStyle(BLRgba32(r, g, b, a));
        ctx.fillRoundRect(BLRoundRect(card_x, card_y, card_w, card_h, bg_radius, bg_radius));
    }

    const auto rgba32 = [](const Color& color) {
        const uint32_t a = static_cast<uint32_t>(std::clamp(color.a, 0.0f, 1.0f) * 255.0f);
        const uint32_t r = static_cast<uint32_t>(std::clamp(color.r, 0.0f, 1.0f) * 255.0f);
        const uint32_t g = static_cast<uint32_t>(std::clamp(color.g, 0.0f, 1.0f) * 255.0f);
        const uint32_t b = static_cast<uint32_t>(std::clamp(color.b, 0.0f, 1.0f) * 255.0f);
        return BLRgba32(r, g, b, a);
    };
    const BLRgba32 bl_fill = rgba32(fill_color);
    const BLRgba32 bl_stroke = rgba32(stroke_color);

    float text_x = (static_cast<float>(img_w) - content_w) * 0.5f - min_x;
    if (alignment == "left") {
        text_x = has_background
            ? ((static_cast<float>(img_w) - (content_w + pad_x * 2.0f)) * 0.5f + pad_x - min_x)
            : (12.0f - min_x);
    } else if (alignment == "right") {
        text_x = has_background
            ? ((static_cast<float>(img_w) + (content_w + pad_x * 2.0f)) * 0.5f - pad_x - content_w - min_x)
            : (static_cast<float>(img_w) - 12.0f - content_w - min_x);
    }
    const float text_y = (static_cast<float>(img_h) - content_h) * 0.5f - min_y;
    for (const auto& span : spans) {
        BLGlyphPlacement placement{};
        placement.placement.reset(0.0, 0.0);
        placement.advance.reset(0.0, 0.0);
        for (std::size_t glyph_index = 0;
             glyph_index < span->glyph_ids.size(); ++glyph_index) {
            std::uint32_t glyph_id = span->glyph_ids[glyph_index];
            BLGlyphRun run{};
            run.glyphData = &glyph_id;
            run.glyphAdvance = static_cast<std::int8_t>(sizeof(glyph_id));
            run.placementData = &placement;
            run.placementAdvance = static_cast<std::int8_t>(sizeof(placement));
            run.placementType = BL_GLYPH_PLACEMENT_TYPE_ADVANCE_OFFSET;
            run.size = 1;
            BLPoint origin = span->origins[glyph_index];
            origin.x += text_x;
            origin.y += text_y;
            if (stroke_width > 0.0f && stroke_color.a > 0.0f) {
                ctx.setStrokeStyle(bl_stroke);
                ctx.setStrokeWidth(stroke_width);
                ctx.strokeGlyphRun(origin, span->font, run);
            }
            ctx.setFillStyle(bl_fill);
            ctx.fillGlyphRun(origin, span->font, run);
        }
    }
    ctx.end();

    BLImageData bl_data;
    bl_img.getData(&bl_data);
    std::vector<float> gpu_rgba(static_cast<size_t>(img_w) * img_h * 4);
    const uint8_t* src = static_cast<const uint8_t*>(bl_data.pixelData);
    size_t out = 0;
    for (int y = 0; y < img_h; ++y) {
        const uint8_t* row = src + y * bl_data.stride;
        for (int x = 0; x < img_w; ++x) {
            const float b_byte = row[x * 4 + 0];
            const float g_byte = row[x * 4 + 1];
            const float r_byte = row[x * 4 + 2];
            const float a_byte = row[x * 4 + 3];
            const float a = a_byte / 255.0f;
            if (a <= 1e-5f) {
                gpu_rgba[out++] = 0.0f;
                gpu_rgba[out++] = 0.0f;
                gpu_rgba[out++] = 0.0f;
                gpu_rgba[out++] = 0.0f;
            } else {
                const auto color = Color{
                    (r_byte / 255.0f) / a,
                    (g_byte / 255.0f) / a,
                    (b_byte / 255.0f) / a,
                    a}.to_linear().premultiplied();
                gpu_rgba[out++] = color.r;
                gpu_rgba[out++] = color.g;
                gpu_rgba[out++] = color.b;
                gpu_rgba[out++] = color.a;
            }
        }
    }

    const std::string_view bytes(
        reinterpret_cast<const char*>(gpu_rgba.data()),
        gpu_rgba.size() * sizeof(float));
    RasterizedTextTexture result;
    result.width = img_w;
    result.height = img_h;
    result.digest = assets::sha256_string(bytes);
    result.gpu_rgba = std::move(gpu_rgba);
    return result;
#endif
}

} // namespace chronon3d::media::video
