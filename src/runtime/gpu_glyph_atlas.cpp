#include <chronon3d/runtime/gpu_glyph_atlas.hpp>

#include <chronon3d/assets/prepared_asset_manifest.hpp>

#include <sstream>

namespace chronon3d::runtime {

namespace {

// Deterministic string form of a glyph key.  The SHA-256 of this string is
// the asset content digest, so the same (font, glyph, size) resolves to the
// same device surface regardless of call order.
std::string glyph_key_string(const GpuGlyphKey& key) {
    std::ostringstream out;
    out << key.font_path << '\x1f' << key.glyph_id << '\x1f' << key.font_size;
    return out.str();
}

} // namespace

void GpuGlyphAtlas::attach(RenderSurfaceRegistry& registry,
                           graph::RenderBackend& backend) noexcept {
    m_cache.attach(registry, backend);
}

GpuGlyphLookup GpuGlyphAtlas::acquire(GpuGlyphKey key,
                                      std::uint32_t width, std::uint32_t height,
                                      std::span<const float> rgba,
                                      GpuGlyphMetrics metrics) {
    GpuAssetKey asset_key;
    asset_key.content_digest = assets::sha256_string(glyph_key_string(key));
    asset_key.format = PixelFormat::Rgba32Float;
    asset_key.width = width;
    asset_key.height = height;

    SurfaceDesc desc{width, height, PixelFormat::Rgba32Float,
                     ResourceUsage::Storage, LifetimeClass::JobPersistent, 0};
    const auto result = m_cache.acquire(asset_key, desc, rgba);
    if (!result.ok()) {
        return {kInvalidRenderSurfaceHandle, metrics, false, result.error};
    }

    {
        std::lock_guard lock(m_mutex);
        const auto [it, inserted] = m_metrics.try_emplace(key, metrics);
        if (!inserted) {
            metrics = it->second;  // prefer the metrics stored on first use
        }
        m_stats.entries = m_metrics.size();
        if (result.cache_hit) {
            ++m_stats.hits;
        } else {
            ++m_stats.misses;
        }
    }
    return {result.handle, metrics, result.cache_hit, {}};
}

std::optional<GpuGlyphMetrics> GpuGlyphAtlas::metrics(GpuGlyphKey key) const noexcept {
    std::lock_guard lock(m_mutex);
    const auto it = m_metrics.find(key);
    if (it == m_metrics.end()) {
        return std::nullopt;
    }
    return it->second;
}

void GpuGlyphAtlas::clear() noexcept {
    std::lock_guard lock(m_mutex);
    m_metrics.clear();
    m_stats = {};
    m_cache.clear();
}

void GpuGlyphAtlas::set_budget_bytes(std::size_t budget_bytes) noexcept {
    m_cache.set_budget_bytes(budget_bytes);
}

GpuGlyphAtlasStats GpuGlyphAtlas::stats() const noexcept {
    std::lock_guard lock(m_mutex);
    return m_stats;
}

} // namespace chronon3d::runtime
