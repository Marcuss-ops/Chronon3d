// ═══════════════════════════════════════════════════════════════════════════
// src/text/text_layout_cache.cpp — M1.5#4
//
//   Extracted from src/text/text_run.cpp.  Contains:
//     - TextLayoutCacheKey::digest()
//     - TextLayoutCacheKeyHash::operator()
//     - TextLayoutCache::Impl + all member functions
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/text_run.hpp>
#include <chronon3d/cache/lru_cache.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>

#include <shared_mutex>
#include <mutex>

namespace chronon3d {

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
    seed = hash_combine(seed, hash_string(features));

    // Paragraph-level typography
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
}

} // namespace chronon3d
