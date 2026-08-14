#pragma once

#include <chronon3d/runtime/gpu_asset_cache.hpp>
#include <chronon3d/runtime/render_surface.hpp>

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>

namespace chronon3d::runtime {

/// VRAM-resident glyph identity.  Glyphs are deterministic per
/// (font_path, glyph_id, font_size) at a fixed rasterizer, so this triple is
/// the stable cache key that the CPU glyph atlas (TextRenderResources) and the
/// device-resident atlas share.
struct GpuGlyphKey {
    std::string font_path;
    std::uint32_t glyph_id{0};
    std::uint32_t font_size{0};

    friend bool operator==(const GpuGlyphKey&, const GpuGlyphKey&) = default;
};

struct GpuGlyphKeyHash {
    std::size_t operator()(const GpuGlyphKey& key) const noexcept {
        std::size_t result = std::hash<std::string>{}(key.font_path);
        result ^= static_cast<std::size_t>(key.glyph_id) +
            static_cast<std::size_t>(0x9e3779b9u) + (result << 6u) + (result >> 2u);
        result ^= static_cast<std::size_t>(key.font_size) +
            static_cast<std::size_t>(0x9e3779b9u) + (result << 6u) + (result >> 2u);
        return result;
    }
};

/// Pen-relative placement of a glyph, preserved across the CPU→GPU promotion
/// so a VRAM-resident glyph can be composited without re-shaping.
struct GpuGlyphMetrics {
    int x_offset{0};
    int y_offset{0};
    float advance_x{0.0f};
};

struct GpuGlyphLookup {
    RenderSurfaceHandle handle{kInvalidRenderSurfaceHandle};
    GpuGlyphMetrics metrics{};
    bool cache_hit{false};
    std::string error;

    [[nodiscard]] bool ok() const noexcept {
        return handle != kInvalidRenderSurfaceHandle && error.empty();
    }
};

struct GpuGlyphAtlasStats {
    std::size_t entries{0};
    std::size_t hits{0};
    std::size_t misses{0};
};

/// VRAM-resident glyph atlas.
///
/// Promotes rasterized glyph bitmaps to device-local surfaces and keeps them
/// resident so a glyph is uploaded once and reused across text layers — the
/// "GlyphAtlas in VRAM" primitive of the GPU overlay-factory plan.  This type
/// is deliberately NOT a second cache: residency, LRU ordering and byte-budget
/// eviction are owned by the canonical GpuAssetCache; the atlas only adds
/// glyph keying (font_path + glyph_id + font_size) and the per-glyph placement
/// metrics the cache does not model.
class GpuGlyphAtlas {
public:
    void attach(RenderSurfaceRegistry& registry,
                graph::RenderBackend& backend) noexcept;

    /// Resolve a glyph to a device-local surface, uploading on first use.
    /// `rgba` must be `width * height * 4` premultiplied floats matching the
    /// backend's Rgba32Float surface format.  `metrics` is stored on first
    /// acquisition and returned on later hits so callers can composite the
    /// glyph without re-shaping.
    GpuGlyphLookup acquire(GpuGlyphKey key,
                           std::uint32_t width, std::uint32_t height,
                           std::span<const float> rgba,
                           GpuGlyphMetrics metrics);

    /// Placement metrics for a previously-acquired glyph, without touching the
    /// device.  Returns nullopt for a glyph never acquired (or cleared).
    [[nodiscard]] std::optional<GpuGlyphMetrics> metrics(GpuGlyphKey key) const noexcept;

    void clear() noexcept;
    void set_budget_bytes(std::size_t budget_bytes) noexcept;
    [[nodiscard]] GpuGlyphAtlasStats stats() const noexcept;

private:
    mutable std::mutex m_mutex;
    GpuAssetCache m_cache;
    std::unordered_map<GpuGlyphKey, GpuGlyphMetrics, GpuGlyphKeyHash> m_metrics;
    GpuGlyphAtlasStats m_stats{};
};

} // namespace chronon3d::runtime
