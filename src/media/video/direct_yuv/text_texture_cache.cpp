#include "text_texture_cache.hpp"

#ifdef CHRONON3D_USE_BLEND2D
#include <blend2d.h>
#endif

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <string_view>

namespace chronon3d::cli {
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
    const std::string& text,
    const std::string& prepared_font_path,
    float font_size,
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
    (void)text; (void)prepared_font_path; (void)font_size; (void)fill_color;
    (void)stroke_color; (void)stroke_width; (void)has_background; (void)bg_color;
    (void)bg_opacity; (void)bg_radius; (void)pad_x; (void)pad_y; (void)box_w;
    (void)box_h; (void)alignment;
    throw std::runtime_error("Blend2D is required for direct text rasterization");
#else
    const std::string font_file = require_prepared_font(prepared_font_path);
    BLFontFace face;
    const BLResult face_res = face.createFromFile(font_file.c_str());
    if (face_res != BL_SUCCESS) {
        throw std::runtime_error("prepared font asset could not be loaded: " + font_file);
    }

    const float effective_font_size = font_size > 0.0f ? font_size : 58.0f;
    BLFont font;
    font.createFromFace(face, effective_font_size);
    const BLFontMetrics fm = font.metrics();

    std::vector<std::string> lines;
    size_t start = 0;
    while (start < text.size()) {
        const size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    if (lines.empty()) lines.push_back(text.empty() ? " " : text);

    std::vector<float> line_widths;
    float max_line_w = 0.0f;
    for (const auto& line : lines) {
        BLGlyphBuffer gb;
        gb.setUtf8Text(line.data(), line.size());
        font.shape(gb);
        BLTextMetrics tm{};
        font.getTextMetrics(gb, tm);
        float w = static_cast<float>(tm.advance.x);
        if (w <= 0.0f) w = static_cast<float>(line.size()) * effective_font_size * 0.55f;
        line_widths.push_back(w);
        max_line_w = std::max(max_line_w, w);
    }

    const float line_spacing = fm.ascent + fm.descent + fm.lineGap;
    const float total_text_h = lines.size() > 1
        ? (fm.ascent + fm.descent + static_cast<float>(lines.size() - 1) * line_spacing)
        : (fm.ascent + fm.descent);

    float target_w = max_line_w;
    float target_h = total_text_h;
    if (has_background) {
        target_w += pad_x * 2.0f;
        target_h += pad_y * 2.0f;
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
        const float card_w = max_line_w + pad_x * 2.0f;
        const float card_h = total_text_h + pad_y * 2.0f;
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

    const float base_y = (static_cast<float>(img_h) - total_text_h) * 0.5f + fm.ascent;
    for (size_t i = 0; i < lines.size(); ++i) {
        const auto& line = lines[i];
        const float line_w = line_widths[i];
        float tx = (static_cast<float>(img_w) - line_w) * 0.5f;
        if (alignment == "left") {
            tx = has_background
                ? ((static_cast<float>(img_w) - (max_line_w + pad_x * 2.0f)) * 0.5f + pad_x)
                : 12.0f;
        } else if (alignment == "right") {
            tx = has_background
                ? ((static_cast<float>(img_w) + (max_line_w + pad_x * 2.0f)) * 0.5f - pad_x - line_w)
                : (static_cast<float>(img_w) - 12.0f - line_w);
        }
        const float ty = base_y + static_cast<float>(i) * line_spacing;
        if (stroke_width > 0.0f && stroke_color.a > 0.0f) {
            ctx.setStrokeStyle(bl_stroke);
            ctx.setStrokeWidth(stroke_width);
            ctx.strokeUtf8Text(BLPoint(tx, ty), font, line.data(), line.size());
        }
        ctx.setFillStyle(bl_fill);
        ctx.fillUtf8Text(BLPoint(tx, ty), font, line.data(), line.size());
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

} // namespace chronon3d::cli
