#pragma once

#include <chronon3d/runtime/render_surface.hpp>
#include <chronon3d/runtime/gpu_glyph_atlas.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace chronon3d::runtime {

/// Location of a glyph inside a paged atlas.
struct PagedGlyphLocation {
    std::uint16_t page{0};   // atlas page index (0, 1, 2, ...)
    std::uint16_t x{0};      // top-left pixel within the page
    std::uint16_t y{0};
    std::uint16_t w{0};      // glyph width (px)
    std::uint16_t h{0};      // glyph height (px)
    float u0{0.0f};         // normalised UV left
    float v0{0.0f};         // normalised UV top
    float u1{1.0f};         // normalised UV right
    float v1{1.0f};         // normalised UV bottom
};

/// Statistics for the paged glyph atlas.
struct PagedGlyphAtlasStats {
    std::size_t total_glyphs{0};
    std::size_t resident_glyphs{0};
    std::size_t cache_hits{0};
    std::size_t cache_misses{0};
    std::size_t page_count{0};
    std::size_t pages_allocated{0};
};

/// Global paged glyph atlas — Fase C.
///
/// Allocates coverage-only (R8) glyph bitmaps into 2048×2048 atlas pages so
/// the same glyph used by hundreds of TextRun nodes is rasterised and uploaded
/// once. The colour is stored per-instance (GlyphInstance::rgba), so \"A rossa\",
/// \"A bianca\", and \"A gialla\" share the same atlas page entry.
///
/// Styled glyphs (stroke / shadow / glow) are NOT stored here — they remain
/// on the existing GpuTextAtlasCache per-phrase packed atlas.
///
/// Thread-safe.  Pages are backed by RenderSurfaceRegistry + RenderBackend;
/// attach() wires these at runtime (same pattern as GpuGlyphAtlas).
class GpuPagedGlyphAtlas {
public:
    static constexpr std::uint32_t kPageSize = 2048;

    void attach(RenderSurfaceRegistry& registry,
                graph::RenderBackend& backend) noexcept;

    /// Resolve a glyph into its page location, uploading on first use.
    /// `coverage` must be `width * height` bytes (R8 alpha/coverage).
    /// Returns the page + UV location and whether this was a cache hit.
    PagedGlyphLocation acquire(const GpuGlyphKey& key,
                               std::uint32_t width,
                               std::uint32_t height,
                               std::span<const std::uint8_t> coverage);

    /// Look up without touching the device (returns nullopt for unknown glyphs).
    [[nodiscard]] std::optional<PagedGlyphLocation> lookup(const GpuGlyphKey& key) const noexcept;

    /// Bind a page handle to a logical surface for shader access.
    /// Callers iterate from 0..page_count()-1 and bind each page.
    [[nodiscard]] RenderSurfaceHandle page_handle(std::size_t page_index) const noexcept;

    /// Number of allocated atlas pages.
    [[nodiscard]] std::size_t page_count() const noexcept { return m_pages.size(); }

    void clear() noexcept;
    [[nodiscard]] PagedGlyphAtlasStats stats() const noexcept;

private:
    struct PageSlot {
        std::uint16_t x{0};
        std::uint16_t y{0};
        std::uint16_t w{0};
        std::uint16_t h{0};
    };

    struct AtlasPage {
        RenderSurfaceHandle handle{kInvalidRenderSurfaceHandle};
        std::uint16_t cursor_x{0};
        std::uint16_t cursor_y{0};
        std::uint16_t row_height{0};
        bool uploaded{false};
    };

    /// Shelf-pack a glyph into existing pages or allocate a new one.
    /// Returns the page index + local pixel coordinates.
    std::pair<std::size_t, PageSlot> allocate_slot(
        std::uint32_t width, std::uint32_t height);

    RenderSurfaceRegistry* m_registry{nullptr};
    graph::RenderBackend* m_backend{nullptr};
    mutable std::mutex m_mutex;
    std::vector<AtlasPage> m_pages;
    std::unordered_map<GpuGlyphKey, PagedGlyphLocation, GpuGlyphKeyHash> m_locations;
    PagedGlyphAtlasStats m_stats{};
};

} // namespace chronon3d::runtime