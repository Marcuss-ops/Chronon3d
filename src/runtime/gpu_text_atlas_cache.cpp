#include <chronon3d/runtime/gpu_text_atlas_cache.hpp>

#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/core/profiling/profiling.hpp>

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
    PackedTextAtlas& out,
    std::string_view stable_identity) {
    std::lock_guard lock(m_mutex);
    out = {};
    if (!m_assets || glyphs.empty()) return false;
    ++m_stats.acquire_calls;

    // Hash the source sequence before packing. The key is independent of
    // destination position, opacity and transform, so animation does not
    // invalidate a static glyph atlas.
    std::string key_bytes;
    key_bytes.reserve(stable_identity.empty() ? glyphs.size() * 16
                                               : stable_identity.size());
    for (const auto& glyph : glyphs) {
        if (glyph.width == 0 || glyph.height == 0 ||
            glyph.rgba.size() != static_cast<std::size_t>(glyph.width) * glyph.height * 4) {
            return false;
        }
        if (stable_identity.empty()) {
            key_bytes.append(reinterpret_cast<const char*>(&glyph.width), sizeof(glyph.width));
            key_bytes.append(reinterpret_cast<const char*>(&glyph.height), sizeof(glyph.height));
            key_bytes.append(reinterpret_cast<const char*>(glyph.rgba.data()), glyph.rgba.size_bytes());
        }
    }
    if (!stable_identity.empty()) {
        key_bytes.assign(stable_identity.data(), stable_identity.size());
    }
    m_stats.key_bytes_hashed += key_bytes.size();
    if (profiling::g_current_counters) {
        profiling::g_current_counters->gpu_text_atlas_key_bytes_hashed.fetch_add(
            static_cast<std::uint64_t>(key_bytes.size()), std::memory_order_relaxed);
    }
    const Key key{assets::sha256_string(key_bytes)};

    auto it = m_entries.find(key);
    const bool cache_hit = it != m_entries.end();
    if (cache_hit) ++m_stats.cache_hits;
    else ++m_stats.cache_misses;
    if (profiling::g_current_counters) {
        auto& counter = cache_hit
            ? profiling::g_current_counters->gpu_text_atlas_cache_hits
            : profiling::g_current_counters->gpu_text_atlas_cache_misses;
        counter.fetch_add(1, std::memory_order_relaxed);
    }
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
        ++m_stats.repack_count;
        m_stats.repack_bytes += static_cast<std::uint64_t>(entry.atlas.size() * sizeof(float));
        it = m_entries.emplace(key, std::move(entry)).first;
    }

    auto& entry = it->second;
    const auto acquired = m_assets->acquire(entry.asset_key, entry.desc, entry.atlas);
    if (!acquired.ok()) return false;
    entry.placement.handle = acquired.handle;
    entry.placement.cache_hit = cache_hit;
    entry.placement.uploaded = !acquired.cache_hit;
    if (!acquired.cache_hit) {
        ++m_stats.asset_upload_count;
        m_stats.asset_upload_bytes += static_cast<std::uint64_t>(entry.atlas.size() * sizeof(float));
    }
    out = entry.placement;
    return true;
}

std::shared_ptr<const GpuTextAtlasCache::StyledGlyphBitmap>
GpuTextAtlasCache::find_styled(std::string_view key_bytes) const {
    const Key key{assets::sha256_string(key_bytes)};
    std::lock_guard lock(m_mutex);
    const auto it = m_styled_entries.find(key);
    if (it == m_styled_entries.end()) return {};
    return std::make_shared<const StyledGlyphBitmap>(it->second.bitmap);
}

void GpuTextAtlasCache::store_styled(
    std::string_view key_bytes, std::uint32_t width, std::uint32_t height,
    std::shared_ptr<const std::vector<float>> rgba) {
    if (!rgba || width == 0 || height == 0) return;
    const Key key{assets::sha256_string(key_bytes)};
    std::lock_guard lock(m_mutex);
    if (m_styled_entries.find(key) != m_styled_entries.end()) return;
    m_styled_entries.emplace(key, StyledEntry{
        key.digest, StyledGlyphBitmap{width, height, std::move(rgba)}});
}

void GpuTextAtlasCache::clear() noexcept {
    std::lock_guard lock(m_mutex);
    m_entries.clear();
    m_styled_entries.clear();
    m_stats = {};
}

std::size_t GpuTextAtlasCache::size() const noexcept {
    std::lock_guard lock(m_mutex);
    return m_entries.size();
}

GpuTextAtlasCache::Stats GpuTextAtlasCache::stats() const noexcept {
    std::lock_guard lock(m_mutex);
    return m_stats;
}

} // namespace chronon3d::runtime
