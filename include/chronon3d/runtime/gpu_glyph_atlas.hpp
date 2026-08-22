#pragma once

#include <chronon3d/runtime/gpu_asset_cache.hpp>
#include <chronon3d/runtime/render_surface.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace chronon3d::runtime {

// Forward-declare legacy types used by deprecated GpuStyledGlyphCache::acquire()
struct PackedGlyphBitmap {
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::span<const float> rgba{};
};
struct PackedTextAtlas {
    RenderSurfaceHandle handle{kInvalidRenderSurfaceHandle};
    std::uint32_t width{0};
    std::uint32_t height{0};
    bool cache_hit{false};
    bool uploaded{false};
    std::vector<std::uint32_t> origin_x;
    std::vector<std::uint32_t> origin_y;
};

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

/// Location of a glyph inside the paged atlas.  Ready for the draw loop —
/// no upload, no shaping, no residency check needed at render time.
struct GlyphLocation {
    std::uint16_t atlas_page{0};
    std::uint16_t uv_index{0};    // index into UV table (resolved by backend)
    std::int16_t local_x{0};
    std::int16_t local_y{0};
    std::uint16_t width{0};
    std::uint16_t height{0};
    float advance_x{0.0f};
};
static_assert(sizeof(GlyphLocation) == 16,
              "GlyphLocation must be compact for SSBO upload");

struct GpuGlyphAtlasStats {
    std::size_t entries{0};
    std::size_t hits{0};
    std::size_t misses{0};
    std::size_t page_count{0};
    std::size_t total_glyph_bytes{0};  // R8 bytes, not RGBA32F
};

/// Global paged glyph atlas — the ONE source of truth for VRAM-resident
/// glyphs.  Every glyph is stored as R8Unorm (1 byte per pixel) in a page.
/// Pages are created lazily and never deleted (job-persistent).
///
/// The atlas stores one `GlyphLocation` per (font, glyph_id, font_size).
/// The location is immutable after first write — the draw loop receives it
/// pre-resolved and never touches the atlas.
class GpuGlyphAtlas {
public:
    static constexpr std::uint32_t kPageSize = 2048;  // 2048×2048 R8 = 4 MiB per page

    void attach(RenderSurfaceRegistry& registry,
                graph::RenderBackend& backend) noexcept;

    /// Resolve a glyph to a GlyphLocation.  On first call, the glyph is
    /// uploaded to the current (or next) atlas page as R8Unorm (1 byte/pixel).
    /// `coverage` must have `width * height` elements (one float 0..1 per pixel).
    /// Returns a ready-to-use GlyphLocation with atlas_page + uv_index set.
    [[nodiscard]] GlyphLocation acquire(GpuGlyphKey key,
                         std::uint32_t width, std::uint32_t height,
                         std::span<const float> coverage,
                         GpuGlyphMetrics metrics);

    /// Placement metrics for a previously-acquired glyph, without touching the
    /// device.  Returns nullopt for a glyph never acquired (or cleared).
    [[nodiscard]] std::optional<GpuGlyphMetrics> metrics(GpuGlyphKey key) const noexcept;

    /// Pre-upload a batch of glyphs in prepare() — moves all rasterization
    /// and upload work before the first frame.
    void prepare_batch(std::span<const GpuGlyphKey> keys,
                       std::span<const std::uint32_t> widths,
                       std::span<const std::uint32_t> heights,
                       std::span<const float> coverage_data,
                       std::span<const GpuGlyphMetrics> metrics);

    void clear() noexcept;
    void set_budget_bytes(std::size_t budget_bytes) noexcept;
    [[nodiscard]] GpuGlyphAtlasStats stats() const noexcept;

private:
    struct AtlasPage {
        RenderSurfaceHandle handle{kInvalidRenderSurfaceHandle};
        std::uint32_t cursor_x{0};
        std::uint32_t cursor_y{0};
        std::uint32_t row_height{0};
    };

    [[nodiscard]] AtlasPage& current_page_or_new();

    mutable std::mutex m_mutex;
    GpuAssetCache m_cache;
    std::vector<AtlasPage> m_pages;
    std::unordered_map<GpuGlyphKey, GlyphLocation, GpuGlyphKeyHash> m_locations;
    std::unordered_map<GpuGlyphKey, GpuGlyphMetrics, GpuGlyphKeyHash> m_metrics;
    GpuGlyphAtlasStats m_stats{};
};

// ── GpuStyledGlyphCache (formerly GpuTextAtlasCache) ───────────────────────
//
/// Owns pre-rasterized styled glyph bitmaps (stroke, shadow, glow).
/// This is a CPU-side cache only — no GPU surface or atlas packing.
/// The packed phrase-atlas concept is gone; styled glyphs live in this cache
/// and are uploaded to GpuGlyphAtlas pages individually.
class GpuStyledGlyphCache {
public:
    struct Stats {
        std::uint64_t acquire_calls{0};
        std::uint64_t cache_hits{0};
        std::uint64_t cache_misses{0};
    };

    struct StyledGlyphBitmap {
        std::uint32_t width{0};
        std::uint32_t height{0};
        std::shared_ptr<const std::vector<float>> rgba;
    };

    /// Returns a previously rasterized/styled glyph bitmap. The returned
    /// pixel storage is immutable and shared by all frames using the key.
    [[nodiscard]] std::shared_ptr<const StyledGlyphBitmap> find_styled(
        std::string_view key_bytes) const;

    /// Stores one CPU-rasterized/styled glyph for reuse by later frames.
    void store_styled(std::string_view key, std::uint32_t width,
                      std::uint32_t height,
                      std::shared_ptr<const std::vector<float>> rgba);

    /// DEPRECATED — phrase-atlas packing is retired. This always returns
    /// false so callers take the per-glyph GpuGlyphAtlas path.
    [[deprecated("Use per-glyph GpuGlyphAtlas instead; this always returns false")]]
    [[nodiscard]] bool acquire(std::span<const PackedGlyphBitmap> /*bitmaps*/,
                               PackedTextAtlas& /*atlas*/,
                               std::string_view /*identity*/ = {}) {
        return false;
    }

    void clear() noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] Stats stats() const noexcept;

private:
    struct Key {
        std::uint64_t h0{0};
        std::uint64_t h1{0};
        friend bool operator==(const Key&, const Key&) = default;
    };
    struct KeyHash {
        std::size_t operator()(const Key& k) const noexcept {
            return static_cast<std::size_t>(k.h0 ^ k.h1);
        }
    };

    struct Entry {
        StyledGlyphBitmap bitmap{};
    };

    mutable std::mutex m_mutex;
    std::unordered_map<Key, Entry, KeyHash> m_entries;
    mutable Stats m_stats{};
};

} // namespace chronon3d::runtime
