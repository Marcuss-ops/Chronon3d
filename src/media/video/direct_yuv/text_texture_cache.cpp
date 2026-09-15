#include "text_texture_cache.hpp"

#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/text/mtsdf_preparation.hpp>
#include <chronon3d/text/mtsdf_profile.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace chronon3d::media::video {
#ifdef CHRONON3D_ENABLE_TEXT
namespace {

std::uint32_t checked_dimension(float value, const char* label) {
    if (!std::isfinite(value) || value <= 0.0f || value > 65535.0f) {
        throw std::runtime_error(std::string("invalid MTSDF atlas ") + label);
    }
    return static_cast<std::uint32_t>(std::ceil(value));
}

// Content-aware key: the layout fingerprint mixed with each run's canonical
// prepared-font content identity.  If /assets/foo.ttf changes bytes during the
// life of a daemon, the packed texture built from the old bytes must not be
// returned as a hit.  The MTSDF glyph cache is content-addressed, but this
// packed lowering cache sits IN FRONT of it, so a path-only key would satisfy
// the lookup here and never reach the digest check at all.
//
// The font identity is not derived here: text::prepared_font_content_digest()
// is the single identity authority (one file hash per font version), so this
// lowering cannot invent a second font-identity notion.
std::uint64_t content_aware_texture_key(
    const TextRunLayout& layout,
    const assets::AssetResolver& resolver) {
    std::uint64_t key = layout.layout_hash();
    for (const auto& span : layout.font_spans) {
        if (span.font.font_path.empty()) continue;
        const auto digest =
            text::prepared_font_content_digest(resolver, span.font.font_path);
        if (digest) {
            for (auto b : digest->bytes) {
                key ^= static_cast<std::uint64_t>(std::to_integer<unsigned char>(b)) +
                    0x9e3779b97f4a7c15ULL + (key << 6U) + (key >> 2U);
            }
        }
        // Mix the span's glyph coverage so two spans that share a font but
        // cover different glyph ranges cannot collide on the same key.
        key ^= static_cast<std::uint64_t>(span.glyph_end - span.glyph_begin) +
            0x9e3779b97f4a7c15ULL + (key << 6U) + (key >> 2U);
    }
    return key;
}

} // namespace

RasterizedTextTexture rasterize_text_texture(
    const TextRunShape& prepared_shape,
    const assets::AssetResolver& resolver) {
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

    constexpr auto profile = text::kCanonicalMtsdfProfile;
    constexpr std::uint16_t kEmPx = profile.em_px;
    constexpr std::uint16_t kRangePx = profile.distance_range_px;
    constexpr std::uint16_t kPaddingPx = profile.padding_px;
    const float layout_scale = font_size / static_cast<float>(kEmPx);

    for (const auto& span : layout.font_spans) {
        if (span.glyph_begin > span.glyph_end ||
            span.glyph_end > layout.placed.glyphs.size() ||
            span.font.font_path.empty()) {
            throw std::runtime_error("prepared text font span is invalid");
        }
    }

    // The DirectYUV caller supplies prepared/canonical font paths. The
    // backend-neutral preparation cache performs the resolver lookup and owns
    // all FreeType face lifecycle; this lowering only packs immutable glyph
    // payloads into one contiguous CUDA texture.
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
            const auto bitmap = text::prepare_mtsdf_glyph(
                resolver, layout.font_spans[span_index].font.font_path,
                state.glyph_id, profile);
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
                static_cast<std::size_t>(row) * glyph.bitmap.width;
            for (std::uint32_t column = 0; column < glyph.bitmap.width; ++column) {
                // MtsdfGlyphBitmap stores one bit-preserving float per texel;
                // the float already contains the packed RGBA8 value.  Treating
                // it as four channel floats here rebuilt the atlas from four
                // unrelated texels and made the shadow/fill sample the glyph
                // plane instead of the glyph coverage.
                destination[column] = source[column];
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
    const TextRunShape&, const assets::AssetResolver&) {
    throw std::runtime_error("DirectYUV text requires CHRONON3D_ENABLE_TEXT");
}

#endif

std::uint64_t DirectYuvTextTextureCache::key_for(
    const TextRunLayout& layout, const assets::AssetResolver* resolver) {
    return resolver != nullptr ? content_aware_texture_key(layout, *resolver)
                               : layout.layout_hash();
}

std::shared_ptr<const RasterizedTextTexture> DirectYuvTextTextureCache::find(
    const TextRunLayout& layout,
    const assets::AssetResolver& resolver) const {
    const auto value = entries_.get(key_for(layout, &resolver));
    return value ? *value : nullptr;
}

std::shared_ptr<const RasterizedTextTexture> DirectYuvTextTextureCache::find(
    const TextRunLayout& layout) const {
    const auto value = entries_.get(key_for(layout, nullptr));
    return value ? *value : nullptr;
}

void DirectYuvTextTextureCache::store(
    const TextRunLayout& layout, const assets::AssetResolver& resolver,
    std::shared_ptr<const RasterizedTextTexture> texture) {
    if (!texture || texture->gpu_rgba.empty()) return;
    const auto weight = sizeof(RasterizedTextTexture) +
        texture->gpu_rgba.size() * sizeof(float);
    entries_.put(key_for(layout, &resolver), std::move(texture), weight);
}

void DirectYuvTextTextureCache::store(
    const TextRunLayout& layout,
    std::shared_ptr<const RasterizedTextTexture> texture) {
    if (!texture || texture->gpu_rgba.empty()) return;
    const auto weight = sizeof(RasterizedTextTexture) +
        texture->gpu_rgba.size() * sizeof(float);
    entries_.put(key_for(layout, nullptr), std::move(texture), weight);
}

std::shared_ptr<const RasterizedTextTexture>
DirectYuvTextTextureCache::get_or_create(
    const TextRunShape& prepared_shape,
    const assets::AssetResolver& resolver) {
    if (!prepared_shape.layout) {
        throw std::runtime_error("DirectYUV text cache requires a prepared layout");
    }
    const auto key = key_for(*prepared_shape.layout, &resolver);
    return entries_.compute_if_absent(key, [&]() {
        auto texture = std::make_shared<RasterizedTextTexture>(
            rasterize_text_texture(prepared_shape, resolver));
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
