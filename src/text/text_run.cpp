#include <chronon3d/text/text_run.hpp>
#include <chronon3d/text/font_engine.hpp>  // P0-2 — font_layout_identity_of(TextRunLayout)
#include <chronon3d/cache/lru_cache.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>

// PR 10 — sample-time fold of AnimatedTextDocument for cache-key
// invalidation.  We fold a small subset of ActiveTextState into the hash
// so Scramble / Morph / CrossfadeLayouts frames don't share stale
// entries.  No behavioural mutations: `hash_text_run_shape` remains pure
// (no reshaping, no cache writes), only mirrors what the per-frame
// driver will SEE when it executes the frame.
#include <chronon3d/text/animated_text_document.hpp>

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
}

// ═══════════════════════════════════════════════════════════════════════════
// P1-DEPRECATED — process-wide singleton (P1 #3 migration in progress)
// ═══════════════════════════════════════════════════════════════════════════
//
// The canonical layout cache now lives on RenderSession::get_layout_cache().
// This singleton remains ONLY for backward compatibility during the
// migration window.  New code MUST thread a RenderSession& or
// TextLayoutCache* through the call chain instead.
//
// Callsite migration (post-baseline):
//   src/scene/builders/text_run_builder.cpp:380  → session.get_layout_cache()
//   src/text/text_run_driver.cpp:112,270,480     → session.get_layout_cache()
//   include/chronon3d/text/rich_text.hpp:238     → deprecate entire file (P1 #4)
//   apps/chronon3d_cli/.../text_audit_engine.cpp → standalone TextLayoutCache
//   tests/text/*.cpp                             → standalone or session
//
// Full census + per-callsite plan in docs/FOLLOWUP_TICKETS.md (P1 #3 row).

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

// ═════════════════════════════════════════════════════════════════════════
// hash_text_run_shape (frame overload) — PR 10
// ═════════════════════════════════════════════════════════════════════════
//
// Mirrors the per-frame driver's view of the shape.  When
// `shape.animated_doc` is bound, this overload samples the doc at `frame`
// and folds a subset of ActiveTextState into the hash so Scramble /
// Morph / CrossfadeLayouts frames invalidate the executor's per-frame
// node cache without false hits.
//
// Fold matrix (PR 10 + PR 11):
//   - transition_type (Y)         — drives the renderer branch.
//   - active->utf8_bytes (Y)      — Hold / Cut / CrossfadeLayouts have
//                                    empty transition_text; we need the
//                                    underlying active document to differ.
//   - active->defaults.font (Y)   — a font-only Cut (text unchanged,
//                                    weight swapped) must invalidate.
//                                    Captures the intent of the next
//                                    apply_active_state_to_text_run_shape.
//   - crossfade_from->utf8 + font (Y, PR 11)
//                                  — CrossfadeLayouts renders BOTH
//                                    active and crossfade_from per-
//                                    glyph in the compositor (PR 11
//                                    implementation).  A change in
//                                    outgoing text or outgoing font
//                                    must invalidate the cache key.
//   - transition_text_bytes (Y)    — random per-frame scramble bytes
//                                    (folded only when non-empty; XXH3
//                                    already discriminates empty vs non-
//                                    empty so we skip the redundant
//                                    length-prefix fold).
//   - morph_map_bytes (Y)          — Morph topology (pair<i32,i32>).
//   - mix_value (Y)                — continuous visual params
//                                    (CrossfadeLayouts fold included).
//   - length-prefix (N)            — XXH3 already absorbs sequence length.
//
// Cost: O(n_keyframes) binary search inside `sample_at` +
// variable-length scramble/morph generation.  In practice keyframe
// lists are tiny (<20) and scramble/morph expansion costs are sub-µs
// for short strings; the folder itself is bounded by a single call per
// node per frame at cache_key() time.

u64 hash_text_run_shape(const TextRunShape& s, Frame frame) {
    using chronon3d::graph::hash_combine;
    using chronon3d::graph::hash_value;
    using chronon3d::graph::hash_string;
    using chronon3d::graph::hash_bytes;

    u64 seed = hash_text_run_shape(s);  // base fold (layout + glyphs + paint + material + shadows)

    // ── PR 10 — fold the AnimatedTextDocument state at `frame` ──────
    //
    // Only meaningful when the shape carries an animated_doc.  Static
    // shapes fall through (no doc fold → the new overload reduces to
    // the legacy one, so non-animated text gets the same hash it
    // always did — safe for downstream consumers).
    if (!s.animated_doc) {
        return seed;
    }

    const ActiveTextState state = s.animated_doc->sample_at(frame);

    // Fold transition_type first so different transition modes can
    // never collide on the same downstream bytes (Scramble vs Morph
    // with identical-looking bytes is detectable only here).
    seed = hash_combine(seed, hash_value(static_cast<u8>(state.transition)));

    // Fold the active document's utf8 + font.  Both cover Hold / Cut /
    // CrossfadeLayouts (where transition_text is empty) AND serve as
    // belt-and-braces discriminators for the scramble/morph cases
    // (where the LAYOUT cache key itself is keyed on
    // layout->source_text once the per-frame driver applies the new
    // active state).  The font fold catches font-swap keyframes that
    // would otherwise produce identical hashes despite a re-shape.
    if (state.active != nullptr) {
        const auto& d = state.active->defaults;
        seed = hash_combine(seed, hash_string(state.active->utf8));
        seed = hash_combine(seed, hash_string(d.font.font_path));
        seed = hash_combine(seed, hash_string(d.font.font_family));
        seed = hash_combine(seed, hash_value(d.font.font_weight));
        seed = hash_combine(seed, hash_string(d.font.font_style));
        seed = hash_combine(seed, hash_value(d.font.font_size));
    }

    // Fold the crossfade_from side conditional on whether
    // sample_at returned a non-null outgoing document.  PR 11:
    // only CrossfadeLayouts sets this pointer, but we fold
    // unconditionally when set so any future transition mode that
    // populates crossfade_from is correctly covered by the cache
    // key.  Mirrors the active>defaults.font fold field-for-field
    // so a font-only delta on the outgoing side (the path that
    // could otherwise produce a stale shape hash across the gap)
    // invalidates the cache.
    if (state.crossfade_from != nullptr) {
        const auto& d = state.crossfade_from->defaults;
        seed = hash_combine(seed, hash_string(state.crossfade_from->utf8));
        seed = hash_combine(seed, hash_string(d.font.font_path));
        seed = hash_combine(seed, hash_string(d.font.font_family));
        seed = hash_combine(seed, hash_value(d.font.font_weight));
        seed = hash_combine(seed, hash_string(d.font.font_style));
        seed = hash_combine(seed, hash_value(d.font.font_size));
    }

    // Fold transition_text bytes for Scramble / Morph (only when
    // non-empty; XXH3 differs deterministically between empty and any
    // non-empty input so an explicit length prefix is redundant).
    if (!state.transition_text.empty()) {
        seed = hash_combine(
            seed,
            hash_bytes(state.transition_text.data(),
                       state.transition_text.size()));
    }

    // Fold morph_map topology for Morph.  Other transitions leave
    // morph_map empty so the size-fold collapses to a single hash
    // value, but it's intentionally retained (not redundant) because
    // it strongly discriminates between Morph (size > 0) and
    // Hold/Cut/Scramble/CrossfadeLayouts (size == 0).  Kept as a
    // structural classifier independent of `mix` so the two signals
    // can be reasoned about separately.
    seed = hash_combine(
        seed,
        hash_value(static_cast<u64>(state.morph_map.size())));
    for (const auto& pair : state.morph_map) {
        seed = hash_combine(seed, hash_value(static_cast<i64>(pair.first)));
        seed = hash_combine(seed, hash_value(static_cast<i64>(pair.second)));
    }

    // Fold mix as a float — drives continuous visual params even when
    // transition_text is identical (e.g. two consecutive Scramble frames
    // with the same content but different mix values).
    seed = hash_combine(seed, hash_value(state.mix));

    return seed;
}

// ═══════════════════════════════════════════════════════════════════════════
// font_layout_identity_of(TextRunLayout) — P0-2
// ═══════════════════════════════════════════════════════════════════════════

FontLayoutIdentity font_layout_identity_of(const TextRunLayout& layout) noexcept {
    return font_layout_identity_of(
        layout.font, layout.font_size, layout.features);
}

} // namespace chronon3d
