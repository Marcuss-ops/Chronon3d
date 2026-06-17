#include <chronon3d/text/text_run.hpp>
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
    seed = hash_combine(seed, hash_value(font.font_weight));
    seed = hash_combine(seed, hash_string(font.font_style));
    seed = hash_combine(seed, hash_value(font_size));
    seed = hash_combine(seed, hash_value(tracking));
    seed = hash_combine(seed, hash_value(static_cast<int>(wrap)));
    seed = hash_combine(seed, hash_value(static_cast<int>(direction)));
    seed = hash_combine(seed, hash_string(language));
    return seed;
}

u64 TextRunLayout::shaping_hash() const {
    using chronon3d::graph::hash_string;
    using chronon3d::graph::hash_combine;
    using chronon3d::graph::hash_value;

    u64 seed = hash_string(font.font_path);
    seed = hash_combine(seed, hash_value(font.font_weight));
    seed = hash_combine(seed, hash_string(font.font_style));
    seed = hash_combine(seed, hash_value(font_size));
    seed = hash_combine(seed, hash_value(tracking));
    seed = hash_combine(seed, hash_value(static_cast<int>(wrap)));
    seed = hash_combine(seed, hash_value(static_cast<int>(direction)));
    seed = hash_combine(seed, hash_string(language));
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
    seed = hash_combine(seed, hash_value(font_weight));
    seed = hash_combine(seed, hash_string(font_style));
    seed = hash_combine(seed, hash_value(font_size));
    seed = hash_combine(seed, hash_value(tracking));
    seed = hash_combine(seed, hash_value(box_width));
    seed = hash_combine(seed, hash_value(static_cast<int>(wrap)));
    seed = hash_combine(seed, hash_value(static_cast<int>(direction)));
    seed = hash_combine(seed, hash_string(language));
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
}

// ═══════════════════════════════════════════════════════════════════════════
// Singleton
// ═══════════════════════════════════════════════════════════════════════════

TextLayoutCache& shared_text_layout_cache() {
    static TextLayoutCache cache;
    return cache;
}

void reset_shared_text_layout_cache() {
    shared_text_layout_cache().clear();
}

// ═══════════════════════════════════════════════════════════════════════════
// TextRunShape hashing
// ═══════════════════════════════════════════════════════════════════════════

u64 hash_text_run_shape(const TextRunShape& s) {
    using chronon3d::graph::hash_combine;
    using chronon3d::graph::hash_value;
    using chronon3d::graph::hash_color;
    using chronon3d::graph::hash_vec2;

    // Layout hash (text + font + shaping)
    u64 seed = s.layout ? s.layout->layout_hash() : u64(0);

    // Animator hash (per-glyph state)
    for (const auto& g : s.glyphs) {
        seed = hash_combine(seed, hash_value(g.glyph_id));
        seed = hash_combine(seed, hash_value(g.position.x));
        seed = hash_combine(seed, hash_value(g.position.y));
        seed = hash_combine(seed, hash_value(g.position.z));
        seed = hash_combine(seed, hash_value(g.scale.x));
        seed = hash_combine(seed, hash_value(g.scale.y));
        seed = hash_combine(seed, hash_value(g.scale.z));
        seed = hash_combine(seed, hash_value(g.rotation.x));
        seed = hash_combine(seed, hash_value(g.rotation.y));
        seed = hash_combine(seed, hash_value(g.rotation.z));
        seed = hash_combine(seed, hash_value(g.opacity));
        seed = hash_combine(seed, hash_value(g.blur));
        seed = hash_combine(seed, hash_color(g.fill));
        seed = hash_combine(seed, hash_color(g.stroke));
        seed = hash_combine(seed, hash_value(g.stroke_width));
    }

    // Material hash (premium material settings)
    seed = hash_combine(seed, hash_value(s.material.enabled));
    if (s.material.enabled) {
        seed = hash_combine(seed, hash_color(s.material.top_color));
        seed = hash_combine(seed, hash_color(s.material.bottom_color));
        seed = hash_combine(seed, hash_value(s.material.bevel_px));
        seed = hash_combine(seed, hash_value(s.material.emissive));
        seed = hash_combine(seed, hash_value(s.material.use_material_glow));
        seed = hash_combine(seed, hash_value(s.material.glow_radius));
        seed = hash_combine(seed, hash_value(s.material.glow_intensity));
        seed = hash_combine(seed, hash_value(s.material.use_material_shadow));
        seed = hash_combine(seed, hash_value(s.material.shadow_blur));
        seed = hash_combine(seed, hash_value(s.material.shadow_opacity));
    }

    // Paint
    seed = hash_combine(seed, hash_color(s.paint.fill));
    seed = hash_combine(seed, hash_value(s.paint.stroke_enabled));
    seed = hash_combine(seed, hash_color(s.paint.stroke_color));
    seed = hash_combine(seed, hash_value(s.paint.stroke_width));

    // Shadows
    for (const auto& shadow : s.shadows) {
        seed = hash_combine(seed, hash_value(shadow.enabled));
        seed = hash_combine(seed, hash_vec2(shadow.offset));
        seed = hash_combine(seed, hash_value(shadow.blur));
        seed = hash_combine(seed, hash_value(shadow.opacity));
        seed = hash_combine(seed, hash_color(shadow.color));
    }

    return seed;
}

} // namespace chronon3d
