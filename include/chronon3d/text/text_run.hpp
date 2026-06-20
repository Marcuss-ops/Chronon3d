#pragma once

#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/text/glyph_selector.hpp>
#include <chronon3d/text/paragraph_style.hpp>
#include <chronon3d/text/text_animator_property.hpp>
#include <chronon3d/text/text_material.hpp>
#include <chronon3d/scene/model/shape/shape.hpp>  // TextPaint, TextShadow
#include <chronon3d/core/types/types.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace chronon3d {

// ═══════════════════════════════════════════════════════════════════════════
// TextRunLayout — immutable layout data for a shaped text run
// ═══════════════════════════════════════════════════════════════════════════
//
// Produced once per unique (text, font, size, tracking, wrap) combination.
// Shared across frames via a std::shared_ptr, so multiple TextRunShape
// instances (or multiple frames of the same animated text) reuse the same
// layout without re-shaping.
//
// Mutability: once created, the layout is immutable. Only the per-frame
// animated glyph states change (stored separately in GlyphInstanceState).

struct TextRunLayout {
    std::string source_text;                     // original UTF-8 source text
    FontSpec font;                               // font identity
    f32 font_size{72.0f};                        // requested font size in pixels

    PlacedGlyphRun placed;                       // shaped + positioned glyphs
    TextUnitMap units;                           // glyph → grapheme/word/line maps

    Vec2 bounds{0.0f, 0.0f};                     // total bounding box (width, height)
    f32 line_height{0.0f};                       // font line height in pixels

    /// Shaping parameters used (for cache key hashing / diagnostics)
    f32 tracking{0.0f};                          // per-cluster tracking in pixels
    TextWrap wrap{TextWrap::None};               // wrapping mode
    TextDirection direction{TextDirection::Auto};
    std::string language;                        // BCP-47 language tag

    /// Compute a hash of the layout content (text + font + shaping + wrapping).
    /// Stable across different materials/strokes on the same text.
    [[nodiscard]] u64 layout_hash() const;

    /// Compute a hash of only the shaping parameters (font, size, wrap, direction).
    /// Excludes the source text — useful for cache partitioning.
    [[nodiscard]] u64 shaping_hash() const;
};

/// Immutable, shared ownership of a TextRunLayout.
/// Multiple frames/components can hold the same pointer without copying.
using SharedTextRunLayout = std::shared_ptr<const TextRunLayout>;

// ═══════════════════════════════════════════════════════════════════════════
// TextRunShape — batched text run with per-glyph animation state
// ═══════════════════════════════════════════════════════════════════════════
//
// Unlike TextShape (which describes a static string with styling), TextRunShape
// combines an immutable shared layout with per-frame animated glyph states.
// This enables After Effects-style text animators (selector + properties)
// to drive glyph-level transforms, opacity, color, blur, etc. without
// creating a separate layer per character.
//
// IMPORTANT: TextRunShape is NOT embedded in the Shape struct (shape.hpp).
// Instead, a dedicated TextRunNode in the render graph will hold it directly.
// This avoids a circular include: fill_style.hpp → shape.hpp → text_run.hpp
// → text_animator_property.hpp → glyph_selector.hpp → animated_value.hpp
// → fill_style.hpp (circular!).

struct TextRunShape {
    SharedTextRunLayout layout;                  // immutable layout (shared across frames)
    std::vector<GlyphInstanceState> glyphs;       // per-glyph animated state

    TextMaterial material;                         // premium material settings
    TextPaint paint;                              // fill/stroke paint settings
    std::vector<TextShadow> shadows;              // per-layer shadow stack
};

// ═══════════════════════════════════════════════════════════════════════════
// TextLayoutCacheKey — key for the immutable layout cache
// ═══════════════════════════════════════════════════════════════════════════
//
// Cached per unique (text, font, size, tracking, wrap, direction, language).
// A hit avoids re-shaping with HarfBuzz + FreeType, which is the dominant
// cost for long/complex text runs.

struct TextLayoutCacheKey {
    std::string text;
    std::string font_path;
    int font_weight{400};
    std::string font_style{"normal"};
    f32 font_size{32.0f};
    f32 tracking{0.0f};
    f32 box_width{0.0f};                         // 0 = no wrapping
    TextWrap wrap{TextWrap::None};
    TextDirection direction{TextDirection::Auto};
    std::string language;                         // BCP-47 language tag

    /// Paragraph-level typography.  Different composer/justification/
    /// indentation/spacing/hanging-punctuation settings produce different
    /// line breaks, so they MUST be part of the cache key.
    ParagraphStyle paragraph{};

    [[nodiscard]] u64 digest() const;
    [[nodiscard]] bool operator==(const TextLayoutCacheKey& other) const = default;
};

/// std::hash for TextLayoutCacheKey.
struct TextLayoutCacheKeyHash {
    [[nodiscard]] size_t operator()(const TextLayoutCacheKey& key) const noexcept;
};

// ═══════════════════════════════════════════════════════════════════════════
// TextLayoutCache — thread-safe LRU cache for TextRunLayout objects
// ═══════════════════════════════════════════════════════════════════════════
//
// Uses the existing LruCache infrastructure (lru_cache.hpp) for eviction
// and thread safety.  Capacity defaults to 64 MiB, tunable via Config.
//
// Typical usage pattern:
//   auto& cache = shared_text_layout_cache();
//   TextLayoutCacheKey key{...};
//   auto layout = cache.find(key);
//   if (!layout) {
//       layout = build_text_run_layout(key);
//       cache.store(key, layout);
//   }

class TextLayoutCache {
public:
    using Result = SharedTextRunLayout;

    explicit TextLayoutCache(size_t capacity_bytes = 64ULL * 1024 * 1024);
    ~TextLayoutCache();

    // Non-copyable (holds unique_ptr to Impl)
    TextLayoutCache(const TextLayoutCache&) = delete;
    TextLayoutCache& operator=(const TextLayoutCache&) = delete;
    TextLayoutCache(TextLayoutCache&&) noexcept;
    TextLayoutCache& operator=(TextLayoutCache&&) noexcept;

    /// Look up a layout. Returns nullptr if not cached.
    [[nodiscard]] Result find(const TextLayoutCacheKey& key) const;

    /// Store a layout in the cache.
    void store(TextLayoutCacheKey key, Result value);

    /// Check if a layout exists without promoting in LRU order.
    [[nodiscard]] bool contains(const TextLayoutCacheKey& key) const;

    /// Erase a specific entry.
    bool erase(const TextLayoutCacheKey& key);

    /// Clear all cached layouts.
    void clear();

    /// Return the number of cached entries.
    [[nodiscard]] size_t size() const;

    /// Hit/miss/eviction stats.
    struct Stats {
        size_t hits{0};
        size_t misses{0};
        size_t evictions{0};
        size_t current_size{0};
        size_t current_weight{0};
    };
    [[nodiscard]] Stats stats() const;

    /// Resize the cache capacity. Evicts LRU entries if tightening.
    void set_capacity(size_t capacity_bytes);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

/// Return the process-wide shared TextLayoutCache singleton.
/// Useful for pipeline stages that don't have access to a LayerBuilder
/// but still want to reuse shaped layouts.
[[nodiscard]] TextLayoutCache& shared_text_layout_cache();

/// Reset the process-wide shared TextLayoutCache singleton.
/// Useful for font hot-reload: call this after updating font files on disk.
void reset_shared_text_layout_cache();

/// Free function to hash a TextRunShape for content fingerprinting.
[[nodiscard]] u64 hash_text_run_shape(const TextRunShape& s);

} // namespace chronon3d
