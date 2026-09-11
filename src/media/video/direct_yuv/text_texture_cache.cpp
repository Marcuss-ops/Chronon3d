#include "text_texture_cache.hpp"

#ifdef CHRONON3D_ENABLE_TEXT
#include <chronon3d/backends/text/mtsdf_generator.hpp>
#endif

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace chronon3d::media::video {
#ifdef CHRONON3D_ENABLE_TEXT
namespace {

FT_Library text_library() {
    static FT_Library library = [] {
        FT_Library result = nullptr;
        if (FT_Init_FreeType(&result) != 0) {
            throw std::runtime_error("MTSDF FreeType initialization failed");
        }
        return result;
    }();
    return library;
}

struct FaceOwner {
    FT_Face face{nullptr};
    FaceOwner() noexcept = default;
    ~FaceOwner() {
        if (face) FT_Done_Face(face);
    }
    FaceOwner(const FaceOwner&) = delete;
    FaceOwner& operator=(const FaceOwner&) = delete;
};

std::uint32_t checked_dimension(float value, const char* label) {
    if (!std::isfinite(value) || value <= 0.0f || value > 65535.0f) {
        throw std::runtime_error(std::string("invalid MTSDF atlas ") + label);
    }
    return static_cast<std::uint32_t>(std::ceil(value));
}

} // namespace

RasterizedTextTexture rasterize_text_texture(
    const TextRunShape& prepared_shape) {
    if (!prepared_shape.layout) {
        throw std::runtime_error("text layer has no prepared TextRunLayout");
    }
    const auto& layout = *prepared_shape.layout;
    if (layout.font_spans.empty() ||
        prepared_shape.glyphs.size() != layout.placed.glyphs.size()) {
        throw std::runtime_error("prepared text MTSDF payload is incomplete");
    }
    std::uint32_t covered_glyphs = 0;
    for (const auto& span : layout.font_spans) {
        if (span.glyph_begin != covered_glyphs ||
            span.glyph_end < span.glyph_begin ||
            span.glyph_end > layout.placed.glyphs.size()) {
            throw std::runtime_error("prepared text font spans are not contiguous");
        }
        covered_glyphs = span.glyph_end;
    }
    if (covered_glyphs != layout.placed.glyphs.size()) {
        throw std::runtime_error("prepared text font spans do not cover the layout");
    }
    const float font_size = layout.font_size;
    if (!(font_size > 0.0f) || !std::isfinite(font_size)) {
        throw std::runtime_error("prepared text has no valid font size");
    }

    constexpr std::uint16_t kEmPx = 64;
    constexpr std::uint16_t kRangePx = 8;
    constexpr std::uint16_t kPaddingPx = 16;
    const float layout_scale = font_size / static_cast<float>(kEmPx);

    std::vector<std::unique_ptr<FaceOwner>> faces;
    faces.reserve(layout.font_spans.size());
    for (const auto& span : layout.font_spans) {
        if (span.glyph_begin > span.glyph_end ||
            span.glyph_end > layout.placed.glyphs.size() ||
            span.font.font_path.empty() ||
            !std::filesystem::exists(span.font.font_path)) {
            throw std::runtime_error("prepared text font span is invalid");
        }
        auto owner = std::make_unique<FaceOwner>();
        if (FT_New_Face(text_library(), span.font.font_path.c_str(), 0,
                        &owner->face) != 0 || !owner->face ||
            FT_Set_Pixel_Sizes(owner->face, kEmPx, kEmPx) != 0) {
            throw std::runtime_error("prepared text font asset could not be loaded: " +
                                     span.font.font_path);
        }
        faces.push_back(std::move(owner));
    }

    struct PreparedGlyph {
        MtsdfGlyphBitmap bitmap;
        float x{0.0f};
        float y{0.0f};
    };
    std::vector<PreparedGlyph> glyphs;
    glyphs.reserve(layout.placed.glyphs.size());
    float min_x = std::numeric_limits<float>::infinity();
    float min_y = std::numeric_limits<float>::infinity();
    float max_x = -std::numeric_limits<float>::infinity();
    float max_y = -std::numeric_limits<float>::infinity();
    for (std::size_t span_index = 0; span_index < layout.font_spans.size(); ++span_index) {
        const auto& span = layout.font_spans[span_index];
        for (std::size_t index = span.glyph_begin; index < span.glyph_end; ++index) {
            const auto& state = prepared_shape.glyphs[index];
            if (state.glyph_id == 0) continue;
            auto bitmap = generate_mtsdf_glyph(
                faces[span_index]->face, state.glyph_id, kEmPx, kRangePx, kPaddingPx);
            if (!bitmap || bitmap->rgba8.empty()) {
                // Spaces and other advance-only glyphs have no outline by
                // design. They still participate in shaping/layout, but do
                // not need an atlas tile. Treating them as a render error
                // made ordinary watermark strings containing spaces fail
                // closed before the GPU compositor was reached.
                continue;
            }
            const auto& placed = layout.placed.glyphs[index];
            PreparedGlyph glyph{std::move(*bitmap),
                                placed.x / layout_scale,
                                placed.y / layout_scale};
            glyph.x += static_cast<float>(glyph.bitmap.x_offset);
            glyph.y += static_cast<float>(glyph.bitmap.y_offset);
            min_x = std::min(min_x, glyph.x);
            min_y = std::min(min_y, glyph.y);
            max_x = std::max(max_x, glyph.x + static_cast<float>(glyph.bitmap.width));
            max_y = std::max(max_y, glyph.y + static_cast<float>(glyph.bitmap.height));
            glyphs.push_back(std::move(glyph));
        }
    }
    if (glyphs.empty() || !std::isfinite(min_x) || !std::isfinite(min_y) ||
        !(max_x > min_x) || !(max_y > min_y)) {
        throw std::runtime_error("prepared text produced no MTSDF glyphs");
    }

    const std::uint32_t atlas_width = checked_dimension(max_x - min_x, "width");
    const std::uint32_t atlas_height = checked_dimension(max_y - min_y, "height");
    std::vector<float> atlas(static_cast<std::size_t>(atlas_width) * atlas_height,
                             0.0f);
    for (const auto& glyph : glyphs) {
        const auto dst_x = static_cast<std::int64_t>(std::floor(glyph.x - min_x));
        const auto dst_y = static_cast<std::int64_t>(std::floor(glyph.y - min_y));
        if (dst_x < 0 || dst_y < 0 ||
            dst_x + glyph.bitmap.width > atlas_width ||
            dst_y + glyph.bitmap.height > atlas_height) {
            throw std::runtime_error("MTSDF glyph does not fit its prepared atlas");
        }
        for (std::uint32_t row = 0; row < glyph.bitmap.height; ++row) {
            auto* destination = atlas.data() +
                (static_cast<std::size_t>(dst_y + row) * atlas_width + dst_x);
            const auto* source = glyph.bitmap.rgba8.data() +
                static_cast<std::size_t>(row) * glyph.bitmap.width * 4u;
            for (std::uint32_t column = 0; column < glyph.bitmap.width; ++column) {
                const auto* pixel = source + static_cast<std::size_t>(column) * 4u;
                const std::uint32_t packed =
                    static_cast<std::uint32_t>(pixel[0]) |
                    (static_cast<std::uint32_t>(pixel[1]) << 8u) |
                    (static_cast<std::uint32_t>(pixel[2]) << 16u) |
                    (static_cast<std::uint32_t>(pixel[3]) << 24u);
                destination[column] = std::bit_cast<float>(packed);
            }
        }
    }

    const std::string_view bytes(
        reinterpret_cast<const char*>(atlas.data()), atlas.size() * sizeof(float));
    RasterizedTextTexture result;
    result.width = static_cast<int>(atlas_width);
    result.height = static_cast<int>(atlas_height);
    result.display_width = static_cast<float>(atlas_width) * layout_scale;
    result.display_height = static_cast<float>(atlas_height) * layout_scale;
    result.distance_range_px = static_cast<float>(kRangePx);
    result.mtsdf = true;
    result.digest = assets::sha256_string(bytes);
    result.gpu_rgba = std::move(atlas);
    return result;
}

#else

RasterizedTextTexture rasterize_text_texture(
    const TextRunShape&) {
    throw std::runtime_error("DirectYUV text requires CHRONON3D_ENABLE_TEXT");
}

#endif

void DirectYuvTextTextureCache::store(
    const TextRunLayout& layout,
    std::shared_ptr<const RasterizedTextTexture> texture) {
    if (!texture || texture->gpu_rgba.empty()) return;
    const auto weight = sizeof(RasterizedTextTexture) +
        texture->gpu_rgba.size() * sizeof(float);
    entries_.put(layout.layout_hash(), std::move(texture), weight);
}

std::shared_ptr<const RasterizedTextTexture>
DirectYuvTextTextureCache::get_or_create(const TextRunShape& prepared_shape) {
    if (!prepared_shape.layout) {
        throw std::runtime_error("DirectYUV text cache requires a prepared layout");
    }
    const auto key = prepared_shape.layout->layout_hash();
    return entries_.compute_if_absent(key, [&]() {
        auto texture = std::make_shared<RasterizedTextTexture>(
            rasterize_text_texture(prepared_shape));
        if (texture->gpu_rgba.empty() || !texture->mtsdf) {
            throw std::runtime_error(
                "DirectYUV text cache received an incomplete MTSDF texture");
        }
        const auto weight = sizeof(RasterizedTextTexture) +
            texture->gpu_rgba.size() * sizeof(float);
        return std::pair{
            std::shared_ptr<const RasterizedTextTexture>(std::move(texture)),
            weight};
    });
}

} // namespace chronon3d::media::video
