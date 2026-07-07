// SPDX-License-Identifier: MIT
#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// M1.5#3 — text_layout_cache.hpp
//
// Single-responsibility sub-header for the immutable layout cache slot
// (key struct + key hash + thread-safe LRU cache class).
//
// Public surface (verbatim moved from the canonical text_run.hpp):
//   - `struct TextLayoutCacheKey`                     (with digest(), ==)
//   - `struct TextLayoutCacheKeyHash`                 (std::hash specialization)
//   - `class TextLayoutCache`                         (LRU cache for SharedTextRunLayout)
//
// No new public classes.  No logic changes.  No signature changes.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/text_run_layout.hpp>     // SharedTextRunLayout, Bcp47LanguageTag, TextShapingFeatures
#include <chronon3d/text/font_engine.hpp>          // FontSpec, TextWrap, TextDirection (transitively)
#include <chronon3d/text/paragraph_style.hpp>      // ParagraphStyle

#include <cstddef>                                  // size_t
#include <memory>                                   // unique_ptr<Impl>
#include <string>
#include <vector>

namespace chronon3d {

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
    Bcp47LanguageTag language;                   // BCP-47 language tag (TICKET-103a: alias of std::string)
    TextShapingFeatures features;                // OT shaping features (TICKET-103a: new field)
    std::string font_family;                     // P0-2 — was missing; placed last for aggregate init compat
    std::string variation_axes;                  // M1.5#5 — variable font axes (e.g. "wght=700,wdth=100")

    /// Paragraph-level typography.  Different composer/justification/
    /// indentation/spacing/hanging-punctuation settings produce different
    /// line breaks, so they MUST be part of the cache key.
    ParagraphStyle paragraph{};

    // ── TICKET-TEXT-CLEANUP-6: visual-affecting TextLayoutSpec fields ──
    // These fields affect layout geometry and must be part of the cache
    // key.  Previously missing — two layouts differing only in these
    // fields would collide on the same cache entry.
    f32           box_height{0.0f};              // layout box height (0 = unbounded)
    int           align{1};                      // TextAlign enum (0=Left, 1=Center, 2=Right)
    int           vertical_align{1};             // VerticalAlign enum (0=Top, 1=Middle, 2=Bottom)
    int           anchor{0};                     // TextAnchor enum
    int           centering_mode{0};             // TextCenteringMode enum (0=LayoutBox, 1=PixelInk)
    f32           line_height{1.2f};             // line height multiplier
    int           max_lines{0};                  // 0 = unlimited
    int           overflow{0};                   // TextOverflow enum (0=Clip, 1=Ellipsis)
    bool          ellipsis{false};               // ellipsis truncation

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
//   TextLayoutCache cache;     // local or session-owned (NO process-wide global — Fase B3)
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

// Fase B3 (DONE) — shared_text_layout_cache() REMOVED from public API.
// Production code must pass TextLayoutCache* via the driver functions
// (update_text_run_shape_per_frame, apply_active_state_to_text_run_shape,
// prewarm_text_run_layout_for_frame).  Tests may use a standalone
// TextLayoutCache or RenderSession::layout_cache.  The former global
// still exists as an internal fallback (text_run.cpp file-scope static)
// for backward compat during migration; Phase C removes it entirely.

} // namespace chronon3d
