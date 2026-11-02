// ═══════════════════════════════════════════════════════════════════════════
// src/text/text_layout_cache.cpp — M1.5#4
//
//   Extracted from the retired src/text/text_run.cpp.  Contains:  // drift-class: historical — retired path (see git history)
//     - TextLayoutCacheKey::digest()
//     - TextLayoutCacheKeyHash::operator()
//     - TextLayoutCache::Impl + all member functions
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/text_run.hpp>
#include <chronon3d/cache/lru_cache.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>

#include <shared_mutex>
#include <mutex>
#include <type_traits>

namespace chronon3d {

// ═══════════════════════════════════════════════════════════════════════════
// TextLayoutCacheKey
// ═══════════════════════════════════════════════════════════════════════════

u64 TextLayoutCacheKey::digest_v2() const {
    using chronon3d::graph::hash_string;
    using chronon3d::graph::hash_combine;
    using chronon3d::graph::hash_value;
    const auto hash_semantic = [](const auto& value) {
        using Value = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::is_floating_point_v<Value>) {
            const Value canonical = value == Value{0} ? Value{0} : value;
            return hash_value(canonical);
        } else {
            return hash_value(value);
        }
    };

    // V2 schema: semantic sequence of explicit fields, including the full
    // ParagraphStyle contract. The domain tag invalidates old digests.
    u64 seed = hash_string("chronon.text-layout-key.v2");
    seed = hash_combine(seed, hash_string(text));
    seed = hash_combine(seed, hash_string(font_path));
    seed = hash_combine(seed, hash_string(font_family));   // P0-2
    seed = hash_combine(seed, hash_semantic(font_weight));
    seed = hash_combine(seed, hash_string(font_style));
    seed = hash_combine(seed, hash_semantic(font_size));
    seed = hash_combine(seed, hash_semantic(tracking));
    seed = hash_combine(seed, hash_semantic(box_width));
    seed = hash_combine(seed, hash_semantic(static_cast<int>(wrap)));
    seed = hash_combine(seed, hash_semantic(static_cast<int>(direction)));
    seed = hash_combine(seed, hash_string(language));
    seed = hash_combine(seed, hash_string(features));
    seed = hash_combine(seed, hash_string(variation_axes));  // M1.5#5

    // TICKET-TEXT-CLEANUP-6: visual-affecting TextLayoutSpec fields
    seed = hash_combine(seed, hash_semantic(box_height));
    seed = hash_combine(seed, hash_semantic(align));
    seed = hash_combine(seed, hash_semantic(vertical_align));
    seed = hash_combine(seed, hash_semantic(anchor));
    seed = hash_combine(seed, hash_semantic(centering_mode));
    seed = hash_combine(seed, hash_semantic(line_height));
    seed = hash_combine(seed, hash_semantic(max_lines));
    seed = hash_combine(seed, hash_semantic(overflow));
    seed = hash_combine(seed, hash_semantic(ellipsis));

    // Paragraph-level typography
    seed = hash_combine(seed, hash_semantic(static_cast<u8>(paragraph.composer)));
    seed = hash_combine(seed, hash_semantic(static_cast<u8>(paragraph.justification)));
    seed = hash_combine(seed, hash_semantic(paragraph.left_indent));
    seed = hash_combine(seed, hash_semantic(paragraph.right_indent));
    seed = hash_combine(seed, hash_semantic(paragraph.first_line_indent));
    seed = hash_combine(seed, hash_semantic(paragraph.space_before));
    seed = hash_combine(seed, hash_semantic(paragraph.space_after));
    seed = hash_combine(seed, hash_semantic(paragraph.spacing.word_min));
    seed = hash_combine(seed, hash_semantic(paragraph.spacing.word_desired));
    seed = hash_combine(seed, hash_semantic(paragraph.spacing.word_max));
    seed = hash_combine(seed, hash_semantic(paragraph.spacing.letter_min));
    seed = hash_combine(seed, hash_semantic(paragraph.spacing.letter_desired));
    seed = hash_combine(seed, hash_semantic(paragraph.spacing.letter_max));
    seed = hash_combine(seed, hash_semantic(paragraph.spacing.glyph_scale_min));
    seed = hash_combine(seed, hash_semantic(paragraph.spacing.glyph_scale_desired));
    seed = hash_combine(seed, hash_semantic(paragraph.spacing.glyph_scale_max));
    seed = hash_combine(seed, hash_semantic(paragraph.hanging_punctuation));
    seed = hash_combine(seed, hash_semantic(paragraph.hanging_limit));
    seed = hash_combine(seed, hash_semantic(paragraph.hyphenation));
    seed = hash_combine(seed, hash_semantic(paragraph.minimum_word_length));
    seed = hash_combine(seed, hash_semantic(paragraph.minimum_prefix));
    seed = hash_combine(seed, hash_semantic(paragraph.minimum_suffix));
    seed = hash_combine(seed, hash_semantic(paragraph.widow_lines));
    seed = hash_combine(seed, hash_semantic(paragraph.orphan_lines));
    seed = hash_combine(seed, hash_semantic(static_cast<u8>(paragraph.direction)));
    seed = hash_combine(seed, hash_semantic(static_cast<u8>(paragraph.spacing_collapse)));

    // ParagraphStyle has grown since its original cache digest. Keep the
    // digest contract aligned with its defaulted equality operator.
    seed = hash_combine(seed, hash_string(paragraph.language));
    seed = hash_combine(seed, hash_semantic(paragraph.feature_tags.size()));
    for (const auto& feature : paragraph.feature_tags)
        seed = hash_combine(seed, hash_string(feature));
    seed = hash_combine(seed, hash_semantic(paragraph.variable_axes.size()));
    for (const auto& axis : paragraph.variable_axes) {
        seed = hash_combine(seed, hash_string(axis.tag));
        seed = hash_combine(seed, hash_semantic(axis.value));
    }
    seed = hash_combine(seed, hash_semantic(paragraph.tab_stops.size()));
    for (const auto stop : paragraph.tab_stops)
        seed = hash_combine(seed, hash_semantic(stop));
    seed = hash_combine(seed, hash_semantic(paragraph.kinsoku));
    seed = hash_combine(seed, hash_semantic(paragraph.auto_fit_font_size));
    seed = hash_combine(seed, hash_semantic(paragraph.min_font_size));
    seed = hash_combine(seed, hash_semantic(paragraph.max_font_size));
    seed = hash_combine(seed, hash_semantic(paragraph.discretionary_ligatures));
    seed = hash_combine(seed, hash_semantic(paragraph.drop_cap_height));
    seed = hash_combine(seed, hash_semantic(paragraph.strict_widow_orphan));
    seed = hash_combine(seed, hash_semantic(paragraph.tight_leading));
    seed = hash_combine(seed, hash_string(paragraph.hyphenation_lang));
    seed = hash_combine(seed, hash_semantic(paragraph.justification_tolerance_px));
    seed = hash_combine(seed, hash_semantic(static_cast<u8>(paragraph.paragraph_mark)));
    seed = hash_combine(seed, hash_string(paragraph.paragraph_mark_custom));
    seed = hash_combine(seed, hash_semantic(paragraph.max_lines));
    seed = hash_combine(seed, hash_string(paragraph.ellipsis));

    return seed;
}

u64 TextLayoutCacheKey::digest_v1() const {
    using chronon3d::graph::hash_string;
    using chronon3d::graph::hash_combine;
    using chronon3d::graph::hash_value;

    u64 seed = hash_string(text);
    seed = hash_combine(seed, hash_string(font_path));
    seed = hash_combine(seed, hash_string(font_family));
    seed = hash_combine(seed, hash_value(font_weight));
    seed = hash_combine(seed, hash_string(font_style));
    seed = hash_combine(seed, hash_value(font_size));
    seed = hash_combine(seed, hash_value(tracking));
    seed = hash_combine(seed, hash_value(box_width));
    seed = hash_combine(seed, hash_value(static_cast<int>(wrap)));
    seed = hash_combine(seed, hash_value(static_cast<int>(direction)));
    seed = hash_combine(seed, hash_string(language));
    seed = hash_combine(seed, hash_string(features));
    seed = hash_combine(seed, hash_string(variation_axes));
    seed = hash_combine(seed, hash_value(box_height));
    seed = hash_combine(seed, hash_value(align));
    seed = hash_combine(seed, hash_value(vertical_align));
    seed = hash_combine(seed, hash_value(anchor));
    seed = hash_combine(seed, hash_value(centering_mode));
    seed = hash_combine(seed, hash_value(line_height));
    seed = hash_combine(seed, hash_value(max_lines));
    seed = hash_combine(seed, hash_value(overflow));
    seed = hash_combine(seed, hash_value(ellipsis));
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

u64 TextLayoutCacheKey::digest() const {
    return digest_v2();
}

size_t TextLayoutCacheKeyHash::operator()(const TextLayoutCacheKey& key) const noexcept {
    return static_cast<size_t>(key.digest());
}

// ═══════════════════════════════════════════════════════════════════════════
// TextLayoutCache
// ═══════════════════════════════════════════════════════════════════════════

namespace {

using LayoutCache = cache::LruCache<
    TextLayoutCacheKey, SharedTextRunLayout, TextLayoutCacheKeyHash>;

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
    auto result = m_impl->cache.get(key);
    if (result) {
        return *result;
    }
    return nullptr;
}

void TextLayoutCache::store(TextLayoutCacheKey key, Result value) {
    size_t glyph_count = value ? value->placed.glyphs.size() : 0;
    size_t weight = sizeof(TextRunLayout) + glyph_count * 128;
    std::unique_lock lock(m_impl->mutex);
    m_impl->cache.put(std::move(key), std::move(value), weight);
}

bool TextLayoutCache::contains(const TextLayoutCacheKey& key) const {
    std::shared_lock lock(m_impl->mutex);
    return m_impl->cache.contains(key);
}

bool TextLayoutCache::erase(const TextLayoutCacheKey& key) {
    std::unique_lock lock(m_impl->mutex);
    return m_impl->cache.erase(key);
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
