#pragma once

#include <chronon3d/runtime/gpu_asset_cache.hpp>
#include <chronon3d/runtime/render_surface.hpp>
#include <chronon3d/math/color.hpp>

#include <cstddef>
#include <bit>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace chronon3d::runtime {

/// VRAM-resident glyph identity.  Glyphs are deterministic per
/// Representation of glyph contours inside the GPU atlas.
enum class GlyphRepresentation : std::uint8_t {
    Coverage = 0,   ///< R8 linear coverage bitmap
    Sdf = 1,        ///< Single-channel signed distance field
    Msdf = 2,       ///< Multi-channel signed distance field (RGB)
    Mtsdf = 3       ///< Multi-channel + true signed distance field (RGBA)
};

/// Distance field profile for distance-field representations.
struct DistanceFieldProfile {
    GlyphRepresentation representation{GlyphRepresentation::Mtsdf};
    std::uint16_t em_px{64};
    std::uint16_t distance_range_px{8};
    std::uint16_t padding_px{10};

    friend bool operator==(const DistanceFieldProfile&, const DistanceFieldProfile&) = default;
};

/// VRAM-resident glyph identity.
/// For MTSDF/MSDF/SDF, font_size is NOT in the key (canonical generation scaled at runtime).
/// For Coverage, font_size is retained for raster-size-specific caching.
struct GpuGlyphKey {
    std::string font_path;
    std::uint32_t glyph_id{0};
    std::uint64_t variation_hash{0};
    GlyphRepresentation representation{GlyphRepresentation::Mtsdf};
    std::uint16_t generation_profile{0};
    std::uint32_t font_size{0};

    /// Runtime font size is a layout concern for distance fields.  It is part
    /// of identity only for Coverage, whose pixels are raster-size-specific.
    friend bool operator==(const GpuGlyphKey& a, const GpuGlyphKey& b) noexcept {
        if (a.font_path != b.font_path ||
            a.glyph_id != b.glyph_id ||
            a.variation_hash != b.variation_hash ||
            a.representation != b.representation ||
            a.generation_profile != b.generation_profile) {
            return false;
        }
        return a.representation != GlyphRepresentation::Coverage ||
               a.font_size == b.font_size;
    }
};

struct GpuGlyphKeyHash {
    std::size_t operator()(const GpuGlyphKey& key) const noexcept {
        std::size_t result = std::hash<std::string>{}(key.font_path);
        result ^= static_cast<std::size_t>(key.glyph_id) +
            static_cast<std::size_t>(0x9e3779b9u) + (result << 6u) + (result >> 2u);
        result ^= static_cast<std::size_t>(key.variation_hash) +
            static_cast<std::size_t>(0x9e3779b9u) + (result << 6u) + (result >> 2u);
        result ^= static_cast<std::size_t>(key.representation) +
            static_cast<std::size_t>(0x9e3779b9u) + (result << 6u) + (result >> 2u);
        result ^= static_cast<std::size_t>(key.generation_profile) +
            static_cast<std::size_t>(0x9e3779b9u) + (result << 6u) + (result >> 2u);
        if (key.representation == GlyphRepresentation::Coverage) {
            result ^= static_cast<std::size_t>(key.font_size) +
                static_cast<std::size_t>(0x9e3779b9u) + (result << 6u) + (result >> 2u);
        }
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

/// Location of a glyph inside the paged atlas.
struct GlyphLocation {
    std::uint16_t atlas_page{0};
    std::uint16_t uv_index{0};
    std::uint16_t atlas_x{0};
    std::uint16_t atlas_y{0};
    std::int16_t local_x{0};
    std::int16_t local_y{0};
    std::uint16_t width{0};
    std::uint16_t height{0};
    float plane_left{0.0f};
    float plane_top{0.0f};
    float plane_right{0.0f};
    float plane_bottom{0.0f};
    float advance_x{0.0f};
};

struct AtlasPageInfo {
    GlyphRepresentation representation{GlyphRepresentation::Mtsdf};
    float distance_range_px{8.0f};
    float em_px{64.0f};
    float inv_width{1.0f / 2048.0f};
    float inv_height{1.0f / 2048.0f};
};

struct GpuGlyphAtlasStats {
    std::size_t entries{0};
    std::size_t hits{0};
    std::size_t misses{0};
    std::size_t page_count{0};
    std::size_t total_glyph_bytes{0};
};

/// Global paged glyph atlas — the ONE source of truth for VRAM-resident
/// glyphs. Every glyph is stored as R8Unorm (coverage/SDF) or Rgba8Unorm (MTSDF)
/// in homogeneous atlas pages. Pages are created lazily without host buffer fills.
class GpuGlyphAtlas {
public:
    static constexpr std::uint32_t kPageSize = 2048;  // 2048×2048

    void attach(RenderSurfaceRegistry& registry,
                graph::RenderBackend& backend) noexcept;

    /// Resolve a glyph to a GlyphLocation.
    [[nodiscard]] GlyphLocation acquire(GpuGlyphKey key,
                         std::uint32_t width, std::uint32_t height,
                         std::span<const float> coverage,
                         GpuGlyphMetrics metrics,
                         const DistanceFieldProfile& profile = {});

    /// Placement metrics for a previously-acquired glyph.
    [[nodiscard]] std::optional<GpuGlyphMetrics> metrics(GpuGlyphKey key) const noexcept;

    /// Pre-upload a batch of glyphs in prepare().
    void prepare_batch(std::span<const GpuGlyphKey> keys,
                       std::span<const std::uint32_t> widths,
                       std::span<const std::uint32_t> heights,
                       std::span<const float> coverage_data,
                       std::span<const GpuGlyphMetrics> metrics,
                       const DistanceFieldProfile& profile = {});

    void clear() noexcept;
    void set_budget_bytes(std::size_t budget_bytes) noexcept;
    [[nodiscard]] GpuGlyphAtlasStats stats() const noexcept;

private:
    struct AtlasPage {
        RenderSurfaceHandle handle{kInvalidRenderSurfaceHandle};
        GlyphRepresentation representation{GlyphRepresentation::Coverage};
        DistanceFieldProfile profile{};
        std::uint32_t cursor_x{0};
        std::uint32_t cursor_y{0};
        std::uint32_t row_height{0};
    };

    [[nodiscard]] AtlasPage& current_page_or_new(
        GlyphRepresentation representation,
        const DistanceFieldProfile& profile);

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
struct StyledGlyphKey {
    std::uint64_t font_id{0};
    std::uint32_t glyph_id{0};
    std::uint32_t font_size{0};
    std::uint32_t stroke_radius{0};
    std::uint32_t background_padding{0};
    Color fill{};
    Color background{};
    Color stroke{};
    float stroke_width{0.0f};
    std::uint64_t shadow_hash{0};

    friend bool operator==(const StyledGlyphKey& a,
                           const StyledGlyphKey& b) noexcept {
        return a.font_id == b.font_id && a.glyph_id == b.glyph_id &&
            a.font_size == b.font_size && a.stroke_radius == b.stroke_radius &&
            a.background_padding == b.background_padding && a.fill.r == b.fill.r &&
            a.fill.g == b.fill.g && a.fill.b == b.fill.b && a.fill.a == b.fill.a &&
            a.background.r == b.background.r && a.background.g == b.background.g &&
            a.background.b == b.background.b && a.background.a == b.background.a &&
            a.stroke.r == b.stroke.r && a.stroke.g == b.stroke.g &&
            a.stroke.b == b.stroke.b && a.stroke.a == b.stroke.a &&
            a.stroke_width == b.stroke_width && a.shadow_hash == b.shadow_hash;
    }
};
static_assert(std::is_trivially_copyable_v<StyledGlyphKey>);

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
        const StyledGlyphKey& key) const;

    /// Legacy byte-key adapter retained for callers outside the render graph.
    [[nodiscard]] std::shared_ptr<const StyledGlyphBitmap> find_styled(
        std::string_view key_bytes) const;

    /// Stores one CPU-rasterized/styled glyph for reuse by later frames.
    void store_styled(std::string_view key, std::uint32_t width,
                      std::uint32_t height,
                      std::shared_ptr<const std::vector<float>> rgba);
    void store_styled(const StyledGlyphKey& key, std::uint32_t width,
                      std::uint32_t height,
                      std::shared_ptr<const std::vector<float>> rgba);

    void clear() noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] Stats stats() const noexcept;

private:
    struct Key {
        StyledGlyphKey value{};
        friend bool operator==(const Key&, const Key&) = default;
    };
    struct KeyHash {
        std::size_t operator()(const Key& k) const noexcept {
            std::size_t h = static_cast<std::size_t>(k.value.font_id);
            const auto mix = [&h](std::uint64_t value) {
                h ^= static_cast<std::size_t>(value) +
                    static_cast<std::size_t>(0x9e3779b97f4a7c15ULL) +
                    (h << 6u) + (h >> 2u);
            };
            mix(k.value.glyph_id);
            mix(k.value.font_size);
            mix(k.value.stroke_radius);
            mix(k.value.background_padding);
            mix(static_cast<std::uint32_t>(std::bit_cast<std::uint32_t>(k.value.fill.r)));
            mix(static_cast<std::uint32_t>(std::bit_cast<std::uint32_t>(k.value.fill.g)));
            mix(static_cast<std::uint32_t>(std::bit_cast<std::uint32_t>(k.value.fill.b)));
            mix(static_cast<std::uint32_t>(std::bit_cast<std::uint32_t>(k.value.fill.a)));
            mix(static_cast<std::uint32_t>(std::bit_cast<std::uint32_t>(k.value.background.r)));
            mix(static_cast<std::uint32_t>(std::bit_cast<std::uint32_t>(k.value.background.g)));
            mix(static_cast<std::uint32_t>(std::bit_cast<std::uint32_t>(k.value.background.b)));
            mix(static_cast<std::uint32_t>(std::bit_cast<std::uint32_t>(k.value.background.a)));
            mix(static_cast<std::uint32_t>(std::bit_cast<std::uint32_t>(k.value.stroke.r)));
            mix(static_cast<std::uint32_t>(std::bit_cast<std::uint32_t>(k.value.stroke.g)));
            mix(static_cast<std::uint32_t>(std::bit_cast<std::uint32_t>(k.value.stroke.b)));
            mix(static_cast<std::uint32_t>(std::bit_cast<std::uint32_t>(k.value.stroke.a)));
            mix(static_cast<std::uint32_t>(std::bit_cast<std::uint32_t>(k.value.stroke_width)));
            mix(k.value.shadow_hash);
            return h;
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
