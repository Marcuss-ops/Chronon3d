#include <chronon3d/runtime/gpu_text_atlas_cache.hpp>

#include <chronon3d/assets/prepared_asset_manifest.hpp>

#include <algorithm>
#include <cstring>
#include <string>

namespace chronon3d::runtime {
namespace {

struct PackedPosition { std::uint32_t x{}, y{}; };

std::vector<PackedPosition> pack_positions(
    std::span<const PackedGlyphBitmap> glyphs,
    std::uint32_t& width,
    std::uint32_t& height) {
    width = 1;
    height = 1;
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t row_height = 0;
    std::vector<PackedPosition> positions;
    positions.reserve(glyphs.size());
    for (const auto& glyph : glyphs) {
        if (glyph.width == 0 || glyph.height == 0) return {};
        if (x != 0 && x + glyph.width > 2048) {
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
    std::lock_guard lock(m_mutex);
    out = {};
    if (!m_assets || glyphs.empty()) return false;

    // Hash the source sequence before packing. The key is independent of
    // destination position, opacity and transform, so animation does not
    // invalidate a static glyph atlas.
    std::string key_bytes;
    key_bytes.reserve(glyphs.size() * 16);
    for (const auto& glyph : glyphs) {
        if (glyph.width == 0 || glyph.height == 0 ||
            glyph.rgba.size() != static_cast<std::size_t>(glyph.width) * glyph.height * 4) {
            return false;
        }
        key_bytes.append(reinterpret_cast<const char*>(&glyph.width), sizeof(glyph.width));
        key_bytes.append(reinterpret_cast<const char*>(&glyph.height), sizeof(glyph.height));
        key_bytes.append(reinterpret_cast<const char*>(glyph.rgba.data()), glyph.rgba.size_bytes());
    }
    const Key key{assets::sha256_string(key_bytes)};

    auto it = m_entries.find(key);
    const bool cache_hit = it != m_entries.end();
    if (it == m_entries.end()) {
        std::uint32_t width = 0, height = 0;
        const auto positions = pack_positions(glyphs, width, height);
        if (positions.size() != glyphs.size()) return false;

        Entry entry;
        entry.desc = SurfaceDesc{width, height, PixelFormat::Rgba32Float,
                                 ResourceUsage::Storage,
                                 LifetimeClass::JobPersistent, 0};
        entry.atlas.assign(static_cast<std::size_t>(width) * height * 4, 0.0f);
        entry.placement.width = width;
        entry.placement.height = height;
        entry.placement.origin_x.reserve(positions.size());
        entry.placement.origin_y.reserve(positions.size());
        for (std::size_t i = 0; i < glyphs.size(); ++i) {
            const auto& glyph = glyphs[i];
            const auto pos = positions[i];
            entry.placement.origin_x.push_back(pos.x);
            entry.placement.origin_y.push_back(pos.y);
            for (std::uint32_t row = 0; row < glyph.height; ++row) {
                const auto* source = glyph.rgba.data() +
                    static_cast<std::size_t>(row) * glyph.width * 4;
                auto* destination = entry.atlas.data() +
                    (static_cast<std::size_t>(pos.y + row) * width + pos.x) * 4;
                std::copy_n(source, static_cast<std::size_t>(glyph.width) * 4, destination);
            }
        }
        entry.asset_key = GpuAssetKey{
            key.digest, PixelFormat::Rgba32Float, width, height};
        it = m_entries.emplace(key, std::move(entry)).first;
    }

    auto& entry = it->second;
    const auto acquired = m_assets->acquire(entry.asset_key, entry.desc, entry.atlas);
    if (!acquired.ok()) return false;
    entry.placement.handle = acquired.handle;
    entry.placement.cache_hit = cache_hit;
    entry.placement.uploaded = !acquired.cache_hit;
    out = entry.placement;
    return true;
}

void GpuTextAtlasCache::clear() noexcept {
    std::lock_guard lock(m_mutex);
    m_entries.clear();
}

} // namespace chronon3d::runtime
