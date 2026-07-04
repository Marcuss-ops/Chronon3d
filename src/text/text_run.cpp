#include <chronon3d/text/text_run.hpp>
#include <chronon3d/text/font_engine.hpp>  // P0-2 — font_layout_identity_of(TextRunLayout)
#include <chronon3d/cache/lru_cache.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>

#include <shared_mutex>
#include <mutex>

#define XXH_INLINE_ALL
#include <xxhash.h>

namespace chronon3d {

// ═══════════════════════════════════════════════════════════════════════════
// TextRunLayout hashing
// ═══════════════════════════════════════════════════════════════════════════

u64 TextRunLayout::layout_hash() const {
    using chronon3d::graph::hash_string;
    using chronon3d::graph::hash_combine;
    using chronon3d::graph::hash_value;

    u64 seed = hash_string(source_text);
    seed = hash_combine(seed, hash_string(font.font_path));
    seed = hash_combine(seed, hash_string(font.font_family));   // P0-2
    seed = hash_combine(seed, hash_value(font.font_weight));
    seed = hash_combine(seed, hash_string(font.font_style));
    seed = hash_combine(seed, hash_value(font_size));
    seed = hash_combine(seed, hash_value(tracking));
    seed = hash_combine(seed, hash_value(static_cast<int>(wrap)));
    seed = hash_combine(seed, hash_value(static_cast<int>(direction)));
    seed = hash_combine(seed, hash_string(language));
    // TICKET-103a — OpenType shaping features are part of the layout
    // identity (e.g. "kern=1" vs "kern=0" produce different glyph
    // advances on ligature-heavy runs).
    seed = hash_combine(seed, hash_string(features));
    return seed;
}

u64 TextRunLayout::shaping_hash() const {
    using chronon3d::graph::hash_string;
    using chronon3d::graph::hash_combine;
    using chronon3d::graph::hash_value;

    u64 seed = hash_string(font.font_path);
    seed = hash_combine(seed, hash_string(font.font_family));   // P0-2
    seed = hash_combine(seed, hash_value(font.font_weight));
    seed = hash_combine(seed, hash_string(font.font_style));
    seed = hash_combine(seed, hash_value(font_size));
    seed = hash_combine(seed, hash_value(tracking));
    seed = hash_combine(seed, hash_value(static_cast<int>(wrap)));
    seed = hash_combine(seed, hash_value(static_cast<int>(direction)));
    seed = hash_combine(seed, hash_string(language));
    // TICKET-103a — OT shaping features fold into shaping_hash so
    // compile-time cache partitioning discriminates on feature string.
    seed = hash_combine(seed, hash_string(features));
    return seed;
}

// ═══════════════════════════════════════════════════════════════════════════
// TextLayoutCacheKey
// ═══════════════════════════════════════════════════════════════════════════

u64 TextLayoutCacheKey::digest() const {
    using chronon3d::graph::hash_string;
    using chronon3d::graph::hash_combine;
    using chronon3d::graph::hash_value;

    u64 seed = hash_string(text);
    seed = hash_combine(seed, hash_string(font_path));
    seed = hash_combine(seed, hash_string(font_family));   // P0-2
    seed = hash_combine(seed, hash_value(font_weight));
    seed = hash_combine(seed, hash_string(font_style));
    seed = hash_combine(seed, hash_value(font_size));
    seed = hash_combine(seed, hash_value(tracking));
    seed = hash_combine(seed, hash_value(box_width));
    seed = hash_combine(seed, hash_value(static_cast<int>(wrap)));
    seed = hash_combine(seed, hash_value(static_cast<int>(direction)));
    seed = hash_combine(seed, hash_string(language));
    // TICKET-103a — OpenType shaping features are part of the cache
    // key signature (different feature strings produce different glyph
    // placements for the same (text, font, size) tuple).
    seed = hash_combine(seed, hash_string(features));

    // Paragraph-level typography — different composer/justification/
    // indentation/spacing/hanging-punctuation produce different line breaks.
    seed = hash_combine(seed, hash_value(static_cast<u8>(paragraph.composer)));
    seed = hash_combine(seed, hash_value(static_cast<u8>(paragraph.justification)));
    seed = hash_combine(seed, hash_value(paragraph.left_indent));
    seed = hash_combine(seed, hash_value(paragraph.right_indent));
    seed = hash_combine(seed, hash_value(paragraph.first_line_indent));
    seed = hash_combine(seed, hash_value(paragraph.space_before));
    seed = hash_combine(seed, hash_value(paragraph.space_after));
    seed = hash_combine(seed, hash_value(paragraph.spacing.word_min));
    seed = hash_combine(seed, hash_value(paragraph.spacing.word_desired));
    seed = hash_combine(seed, hash_value(paragraph.spacing.word_max));
    seed = hash_combine(seed, hash_value(paragraph.spacing.letter_min));
    seed = hash_combine(seed, hash_value(paragraph.spacing.letter_desired));
    seed = hash_combine(seed, hash_value(paragraph.spacing.letter_max));
    seed = hash_combine(seed, hash_value(paragraph.spacing.glyph_scale_min));
    seed = hash_combine(seed, hash_value(paragraph.spacing.glyph_scale_desired));
    seed = hash_combine(seed, hash_value(paragraph.spacing.glyph_scale_max));
    seed = hash_combine(seed, hash_value(paragraph.hanging_punctuation));
    seed = hash_combine(seed, hash_value(paragraph.hanging_limit));
    seed = hash_combine(seed, hash_value(paragraph.hyphenation));
    seed = hash_combine(seed, hash_value(paragraph.minimum_word_length));
    seed = hash_combine(seed, hash_value(paragraph.minimum_prefix));
    seed = hash_combine(seed, hash_value(paragraph.minimum_suffix));
    seed = hash_combine(seed, hash_value(paragraph.widow_lines));
    seed = hash_combine(seed, hash_value(paragraph.orphan_lines));
    seed = hash_combine(seed, hash_value(static_cast<u8>(paragraph.direction)));
    seed = hash_combine(seed, hash_value(static_cast<u8>(paragraph.spacing_collapse)));

    return seed;
}

size_t TextLayoutCacheKeyHash::operator()(const TextLayoutCacheKey& key) const noexcept {
    return static_cast<size_t>(key.digest());
}

// ═══════════════════════════════════════════════════════════════════════════
// TextLayoutCache
// ═══════════════════════════════════════════════════════════════════════════

namespace {

using LayoutCache = cache::LruCache<u64, SharedTextRunLayout>;

} // namespace

struct TextLayoutCache::Impl {
    LayoutCache cache;
    std::shared_mutex mutex;

    explicit Impl(size_t capacity_bytes)
        : cache(capacity_bytes, 4) {}
};

TextLayoutCache::TextLayoutCache(size_t capacity_bytes)
    : m_impl(std::make_unique<Impl>(capacity_bytes > 0
          ? capacity_bytes
          : 64ULL * 1024 * 1024)) {}

TextLayoutCache::~TextLayoutCache() = default;
TextLayoutCache::TextLayoutCache(TextLayoutCache&&) noexcept = default;
TextLayoutCache& TextLayoutCache::operator=(TextLayoutCache&&) noexcept = default;

TextLayoutCache::Result TextLayoutCache::find(const TextLayoutCacheKey& key) const {
    std::shared_lock lock(m_impl->mutex);
    auto result = m_impl->cache.get(key.digest());
    if (result) {
        return *result;
    }
    return nullptr;
}

void TextLayoutCache::store(TextLayoutCacheKey key, Result value) {
    // Weight = sizeof(TextRunLayout) + glyph array.  Approximate by
    // the number of glyphs × 128 bytes (PlacedGlyph + GlyphInstanceState).
    size_t glyph_count = value ? value->placed.glyphs.size() : 0;
    size_t weight = sizeof(TextRunLayout) + glyph_count * 128;
    std::unique_lock lock(m_impl->mutex);
    m_impl->cache.put(key.digest(), std::move(value), weight);
}

bool TextLayoutCache::contains(const TextLayoutCacheKey& key) const {
    std::shared_lock lock(m_impl->mutex);
    return m_impl->cache.contains(key.digest());
}

bool TextLayoutCache::erase(const TextLayoutCacheKey& key) {
    std::unique_lock lock(m_impl->mutex);
    return m_impl->cache.erase(key.digest());
}

void TextLayoutCache::clear() {
    std::unique_lock lock(m_impl->mutex);
    m_impl->cache.clear();
}

size_t TextLayoutCache::size() const {
    return m_impl->cache.stats().current_size;
}

TextLayoutCache::Stats TextLayoutCache::stats() const {
    auto s = m_impl->cache.stats();
    return {s.hits, s.misses, s.evictions, s.current_size, s.current_weight};
}

void TextLayoutCache::set_capacity(size_t capacity_bytes) {
    if (capacity_bytes > 0) {
        m_impl->cache.resize(capacity_bytes);
    }
}// ═══════════════════════════════════════════════════════════════════════════
// M1.5#4 Commit A — TextRunShape hashing MOVED to text_run_hash.cpp.
//
// Both `hash_text_run_shape(const TextRunShape&)` and
// `hash_text_run_shape(const TextRunShape&, Frame)` overloads were
// extracted to `src/text/text_run_hash.cpp` (which pairs with
// `include/chronon3d/text/text_run_hash.hpp`, the M1.5#3 umbrella
// sub-header).  No behavioural mutations, no signature changes —
// bodies are verbatim moves; actors that previously called the
// functions continue to link against the same symbols.
//
// This file (text_run.cpp) now hosts ONLY:
//   - TextRunLayout::layout_hash()             (Commit C target)
//   - TextRunLayout::shaping_hash()
//   - TextLayoutCacheKey::digest()             (Commit B target)
//   - TextLayoutCacheKeyHash::operator()
//   - TextLayoutCache::Impl + class methods    (Commit B target)
//   - font_layout_identity_of(TextRunLayout)   (Commit C target)
//
// Future M1.5#4 commits (B, C) split these into text_layout_cache.cpp
// and text_layout_identity.cpp respectively.  This Commit A is the
// first single-responsibility extraction in the chain.
// ═══════════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════════
// font_layout_identity_of(TextRunLayout) — P0-2
// ═══════════════════════════════════════════════════════════════════════════

FontLayoutIdentity font_layout_identity_of(const TextRunLayout& layout) noexcept {
    return font_layout_identity_of(
        layout.font, layout.font_size, layout.features,
        /*variation_axes=*/{});  // M1.5#4 — TextRunLayout lacks variation_axes yet
}

} // namespace chronon3d

