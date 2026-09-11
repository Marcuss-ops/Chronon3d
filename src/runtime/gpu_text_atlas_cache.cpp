#include <chronon3d/runtime/gpu_text_atlas_cache.hpp>

#include <chronon3d/assets/prepared_asset_manifest.hpp>

#include <algorithm>
#include <cstring>
#include <string>

namespace chronon3d::runtime {
namespace {

struct PackedPosition {
    std::uint32_t x{0};
    std::uint32_t y{0};
};

std::vector<PackedPosition> pack_positions(
    std::span<const PackedGlyphBitmap> glyphs,
    std::uint32_t& width,
    std::uint32_t& height) {
    constexpr std::uint32_t kMaxAtlasWidth = 1024;
    width = 1;
    height = 1;
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t row_height = 0;
    std::vector<PackedPosition> positions;
    positions.reserve(glyphs.size());
    for (const auto& glyph : glyphs) {
        if (glyph.width == 0 || glyph.height == 0 ||
            glyph.width > kMaxAtlasWidth ||
            x + glyph.width > kMaxAtlasWidth) {
            if (glyph.width == 0 || glyph.height == 0 ||
                glyph.width > kMaxAtlasWidth) {
                return {};
            }
            y += row_height;
            x = 0;
            row_height = 0;
        }
        positions.push_back({x, y});
        x += glyph.width;
        row_height = std::max(row_height, glyph.height);
        width = std::max(width, x);
    }
    height = std::max(1u, y + row_height);
    return positions;
}

} // namespace

bool GpuTextAtlasCache::acquire(
    std::span<const PackedGlyphBitmap> glyphs,
    PackedTextAtlas& out) {
    out = {};
    if (!m_assets || glyphs.empty()) return false;

    std::string key_bytes;
    key_bytes.reserve(glyphs.size() * 16);
    for (const auto& glyph : glyphs) {
        const auto expected = static_cast<std::size_t>(glyph.width) *
            static_cast<std::size_t>(glyph.height) * 4u;
        if (glyph.width == 0 || glyph.height == 0 ||
            glyph.rgba.size() != expected) {
            return false;
        }
        key_bytes.append(reinterpret_cast<const char*>(&glyph.width),
                         sizeof(glyph.width));
        key_bytes.append(reinterpret_cast<const char*>(&glyph.height),
                         sizeof(glyph.height));
        key_bytes.append(reinterpret_cast<const char*>(glyph.rgba.data()),
                         glyph.rgba.size_bytes());
    }
    const Key key{assets::sha256_string(key_bytes)};

    const auto cached = m_entries.get(key);
    const bool cache_hit = cached && *cached;
    std::shared_ptr<const Entry> entry;
    if (cache_hit) {
        entry = *cached;
    } else {
        entry = m_entries.compute_if_absent(key, [&]() {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        const auto positions = pack_positions(glyphs, width, height);
        if (positions.size() != glyphs.size()) {
            return std::pair<std::shared_ptr<const Entry>, std::size_t>{nullptr, 1};
        }

        auto built = std::make_shared<Entry>();
        auto& value = *built;
        value.desc = SurfaceDesc::make(
            width, height, PixelFormat::Rgba32Float,
            ResourceUsage::Storage, LifetimeClass::JobPersistent);
        value.atlas.assign(static_cast<std::size_t>(width) * height * 4u,
                           0.0f);
        value.placement.width = width;
        value.placement.height = height;
        value.placement.origin_x.reserve(positions.size());
        value.placement.origin_y.reserve(positions.size());
        for (std::size_t i = 0; i < glyphs.size(); ++i) {
            const auto& glyph = glyphs[i];
            const auto pos = positions[i];
            value.placement.origin_x.push_back(pos.x);
            value.placement.origin_y.push_back(pos.y);
            for (std::uint32_t row = 0; row < glyph.height; ++row) {
                const auto* source = glyph.rgba.data() +
                    static_cast<std::size_t>(row) * glyph.width * 4u;
                auto* destination = value.atlas.data() +
                    (static_cast<std::size_t>(pos.y + row) * width + pos.x) * 4u;
                std::copy_n(source, static_cast<std::size_t>(glyph.width) * 4u,
                            destination);
            }
        }
        value.asset_key = GpuAssetKey{
            key.digest, PixelFormat::Rgba32Float, width, height};
        const auto weight = std::max<std::size_t>(
            1, value.atlas.size() * sizeof(float) +
                value.placement.origin_x.size() * sizeof(std::uint32_t) * 2);
        return std::pair<std::shared_ptr<const Entry>, std::size_t>{
            std::move(built), weight};
        });
    }

    if (!entry) return false;

    const auto acquired = m_assets->acquire(
        entry->asset_key, entry->desc, entry->atlas);
    if (!acquired.ok()) return false;
    out = entry->placement;
    out.handle = acquired.handle;
    out.cache_hit = cache_hit && acquired.cache_hit;
    out.uploaded = !acquired.cache_hit;
    return true;
}

void GpuTextAtlasCache::clear() noexcept {
    m_entries.clear();
}

std::size_t GpuTextAtlasCache::size() const noexcept {
    return m_entries.stats().current_size;
}

} // namespace chronon3d::runtime
