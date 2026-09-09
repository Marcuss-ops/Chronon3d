#pragma once

#include <chronon3d/runtime/gpu_asset_cache.hpp>
#include <chronon3d/runtime/render_surface.hpp>
#include <chronon3d/core/types/result.hpp>
#include <chronon3d/core/hash/hash_builder.hpp>
#include <chronon3d/math/color.hpp>

#include <cstddef>
#include <bit>
#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>
#include <xxhash.h>

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

    // FIXING.md C-04 — canonical factory: normalizes font_size=0 for
    // distance-field reps so callers cannot accidentally diverge from
    // the SSOT equality/hash (which ignore font_size for Mtsdf/Msdf/Sdf).
    [[nodiscard]] static GpuGlyphKey make(std::string font_path,
                                          std::uint32_t glyph_id,
                                          std::uint64_t variation_hash,
                                          GlyphRepresentation rep,
                                          std::uint16_t generation_profile,
                                          std::uint32_t font_size) noexcept {
        if (rep != GlyphRepresentation::Coverage) font_size = 0;
        return GpuGlyphKey{std::move(font_path), glyph_id, variation_hash,
                           rep, generation_profile, font_size};
    }

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

// FIXING.md U-01 — unified on core::hash::HashBuilder (XXH64 string + hash_combine).
// GpuStyledGlyphCache::KeyHash below uses the same primitive.
struct GpuGlyphKeyHash {
    std::size_t operator()(const GpuGlyphKey& key) const noexcept {
        auto h = core::hash::HashBuilder{}
            .add(std::string_view(key.font_path))
            .add(key.glyph_id)
            .add(key.variation_hash)
            .add_enum(key.representation)
            .add(key.generation_profile);
        if (key.representation == GlyphRepresentation::Coverage) h.add(key.font_size);
        return static_cast<std::size_t>(h.finish());
    }
};

// AUDIT-2026-09-09 U-01 — the last hand-rolled hash combine is gone: this
// view hash uses the same core::hash::HashBuilder authority as
// GpuGlyphKeyHash above (same field set, same Coverage-only font_size
// rule), so glyph identity has ONE representation in this header. Digest
// values change once (in-memory GPU keys only; no persisted artifact is
// keyed by this hash).
inline std::uint64_t glyph_key_hash64(const GpuGlyphKey& key) noexcept {
    auto h = core::hash::HashBuilder{}
                 .add(std::string_view(key.font_path))
                 .add(key.glyph_id)
                 .add(key.variation_hash)
                 .add_enum(key.representation)
                 .add(key.generation_profile);
    if (key.representation == GlyphRepresentation::Coverage) h.add(key.font_size);
    return h.finish();
}

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

struct GpuGlyphAtlasError {
    enum class Code : std::uint8_t {
        NotAttached,
        InvalidDimensions,
        InvalidCoverage,
        AllocationFailed,
        UploadFailed,
        BudgetExceeded,
    };

    Code code{Code::AllocationFailed};
    std::string message;
};

struct GpuGlyphAtlasStats {
    std::size_t entries{0};
    std::size_t hits{0};
    std::size_t misses{0};
    std::size_t failures{0};
    std::size_t page_count{0};
    std::size_t total_glyph_bytes{0};
};

using GpuGlyphAcquireResult = Result<GlyphLocation, GpuGlyphAtlasError>;

/// Global paged glyph atlas — the ONE source of truth for VRAM-resident
/// glyphs. Every glyph is stored as R8Unorm (coverage/SDF) or Rgba8Unorm (MTSDF)
/// in homogeneous atlas pages. Pages are created lazily without host buffer fills.
class GpuGlyphAtlas {
public:
    static constexpr std::uint32_t kPageSize = 2048;  // 2048×2048

    void attach(RenderSurfaceRegistry& registry,
                graph::RenderBackend& backend) noexcept;

    /// Resolve a glyph to a GlyphLocation.
    ///
    /// Allocation, upload and budget failures are explicit errors. No invalid
    /// GPU handle or speculative atlas location is ever returned or cached.
    [[nodiscard]] GpuGlyphAcquireResult acquire(
        GpuGlyphKey key,
        std::uint32_t width, std::uint32_t height,
        std::span<const float> coverage,
        GpuGlyphMetrics metrics,
        const DistanceFieldProfile& profile = {});

    /// Placement metrics for a previously-acquired glyph.
    [[nodiscard]] std::optional<GpuGlyphMetrics> metrics(GpuGlyphKey key) const noexcept;

    /// Pre-upload a batch of glyphs in prepare().
    [[nodiscard]] Result<std::size_t, GpuGlyphAtlasError> prepare_batch(
        std::span<const GpuGlyphKey> keys,
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

    [[nodiscard]] Result<std::size_t, GpuGlyphAtlasError> page_for_glyph(
        GlyphRepresentation representation,
        const DistanceFieldProfile& profile,
        std::uint32_t width,
        std::uint32_t height);

    // FIXING.md S-01 — shared_mutex: read hits (metrics/stats + future
    // glyph find fast-path) take shared_lock; writes (page alloc/upload,
    // location insert) take unique_lock. Full per-representation sharding
    // (4× shared_mutex + upload outside lock) is deferred — see comment
    // in gpu_glyph_atlas_core_detail.hpp; this is the minimal contention
    // step with Basso rischio that already de-serializes concurrent readers.
    mutable std::shared_mutex m_mutex;
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
    // FIXING.md U-01 — unified on HashBuilder (same primitive as GpuGlyphKeyHash).
    struct KeyHash {
        std::size_t operator()(const Key& k) const noexcept {
            auto h = core::hash::HashBuilder{}
                .add(k.value.font_id)
                .add(k.value.glyph_id)
                .add(k.value.font_size)
                .add(k.value.stroke_radius)
                .add(k.value.background_padding)
                .add(k.value.fill.r).add(k.value.fill.g).add(k.value.fill.b).add(k.value.fill.a)
                .add(k.value.background.r).add(k.value.background.g).add(k.value.background.b).add(k.value.background.a)
                .add(k.value.stroke.r).add(k.value.stroke.g).add(k.value.stroke.b).add(k.value.stroke.a)
                .add(k.value.stroke_width)
                .add(k.value.shadow_hash);
            return static_cast<std::size_t>(h.finish());
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
