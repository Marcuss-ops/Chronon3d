#include <chronon3d/text/glyph_selector.hpp>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <unordered_map>

namespace chronon3d {

// ═══════════════════════════════════════════════════════════════════════════
// TextUnitMap builder
// ═══════════════════════════════════════════════════════════════════════════

TextUnitMap build_text_unit_map(
    const PlacedGlyphRun& placed,
    std::string_view source
) {
    TextUnitMap map;

    const size_t glyph_count = placed.glyphs.size();
    if (glyph_count == 0) return map;

    // ── Size all three arrays ────────────────────────────────────────
    map.glyph_to_grapheme.resize(glyph_count);
    map.glyph_to_word.resize(glyph_count);
    map.glyph_to_line.resize(glyph_count);

    // ── Grapheme mapping: use cluster info ───────────────────────────
    // Each HarfBuzz cluster maps to one grapheme unit.
    // Multiple glyphs within the same cluster share the same grapheme index.
    {
        u32 grapheme_idx = 0;
        u32 prev_cluster = UINT32_MAX;

        for (size_t g = 0; g < glyph_count; ++g) {
            u32 cluster = placed.glyphs[g].cluster;
            if (cluster != prev_cluster) {
                grapheme_idx++;
                prev_cluster = cluster;
            }
            map.glyph_to_grapheme[g] = grapheme_idx - 1;
        }
        map.grapheme_count = grapheme_idx;
    }

    // ── Word and line mapping: walk source text byte-by-byte ─────────
    // For every glyph, we look at the source text at its byte_offset to
    // determine which word and line it belongs to.
    {
        u32 word_idx = 0;
        u32 line_idx = 0;
        bool in_word = false;

        // Build a per-byte word/line index lookup for O(1) mapping.
        // Whitespace and newlines map to word index kNoUnit (skipped).
        // Only non-whitespace runs increment word_idx.
        constexpr u32 kNoUnit = UINT32_MAX;
        const size_t src_len = source.size();
        std::vector<u32> byte_to_word(src_len + 1, kNoUnit);
        std::vector<u32> byte_to_line(src_len + 1, 0);

        for (size_t b = 0; b < src_len;) {
            // Decode the next Unicode code point
            size_t cp_len = 1;
            const char32_t cp = detail::decode_utf8_codepoint_from(source, b, cp_len);
            if (b + cp_len > src_len) cp_len = src_len - b;

            // Check if this code point is a newline or whitespace
            bool is_newline = (cp == u'\n' || cp == 0x2028 || cp == 0x2029);
            bool is_space = (!is_newline && detail::is_unicode_whitespace(cp));

            if (is_newline) {
                if (in_word) { word_idx++; in_word = false; }
                for (size_t j = 0; j < cp_len && (b + j) < src_len; ++j) {
                    byte_to_word[b + j] = kNoUnit;
                }
                line_idx++;
                for (size_t j = 0; j < cp_len && (b + j) < src_len; ++j) {
                    byte_to_line[b + j] = line_idx;
                }
            } else if (is_space) {
                if (in_word) { word_idx++; in_word = false; }
                for (size_t j = 0; j < cp_len && (b + j) < src_len; ++j) {
                    byte_to_word[b + j] = kNoUnit;
                    byte_to_line[b + j] = line_idx;
                }
            } else {
                in_word = true;
                for (size_t j = 0; j < cp_len && (b + j) < src_len; ++j) {
                    byte_to_word[b + j] = word_idx;
                    byte_to_line[b + j] = line_idx;
                }
            }

            b += cp_len;
        }

        // Fill trailing bytes
        for (size_t b = src_len; b <= src_len; ++b) {
            byte_to_word[b] = kNoUnit;
            byte_to_line[b] = line_idx;
        }

        if (in_word) word_idx++;
        map.word_count = word_idx;
        map.line_count = line_idx + 1;

        // Precompute first_glyph_for_word and first_glyph_for_line
        // for O(1) whitespace exclusion in CompiledSelector.
        constexpr u32 kNotFound = UINT32_MAX;
        map.first_glyph_for_word.assign(word_idx, kNotFound);
        map.first_glyph_for_line.assign(line_idx + 1, kNotFound);

        // Map each glyph to its word/line via its byte offset
        for (size_t g = 0; g < glyph_count; ++g) {
            size_t byte_off = placed.glyphs[g].byte_offset;
            if (byte_off > src_len) byte_off = src_len;
            map.glyph_to_word[g] = byte_to_word[byte_off];
            map.glyph_to_line[g] = byte_to_line[byte_off];

            // Populate first_glyph_for_word/line (first seen wins)
            const u32 w = map.glyph_to_word[g];
            if (w != kNoUnit && map.first_glyph_for_word[w] == kNotFound) {
                map.first_glyph_for_word[w] = static_cast<u32>(g);
            }
            const u32 l = map.glyph_to_line[g];
            if (l < map.first_glyph_for_line.size() && map.first_glyph_for_line[l] == kNotFound) {
                map.first_glyph_for_line[l] = static_cast<u32>(g);
            }
        }
    }

    return map;
}

// ═══════════════════════════════════════════════════════════════════════════
// Shape evaluation
// ═══════════════════════════════════════════════════════════════════════════

namespace detail {

f32 evaluate_selector_shape(
    TextSelectorShape shape,
    f32 unit_position,
    f32 start,
    f32 end
) {
    // Clamp inputs
    unit_position = std::clamp(unit_position, 0.0f, 1.0f);
    start = std::clamp(start, 0.0f, 1.0f);
    end   = std::clamp(end, 0.0f, 1.0f);

    // Handle reversed range (start > end)
    if (start > end) {
        std::swap(start, end);
        unit_position = 1.0f - unit_position;
    }

    const f32 range = end - start;

    // Outside the active window — handle edge cases per shape
    if (unit_position <= start) {
        // RampDown is fully active at the left edge
        if (shape == TextSelectorShape::RampDown) return 1.0f;
        return 0.0f;
    }
    if (unit_position >= end) {
        // RampUp is fully active at the right edge
        if (shape == TextSelectorShape::RampUp) return 1.0f;
        return 0.0f;
    }

    // Normalised position within [start, end]
    const f32 t = (range > 1e-7f) ? ((unit_position - start) / range) : 0.5f;

    switch (shape) {
        case TextSelectorShape::Square:
            return 1.0f;

        case TextSelectorShape::RampUp:
            return t;

        case TextSelectorShape::RampDown:
            return 1.0f - t;

        case TextSelectorShape::Triangle: {
            // Peak at t=0.5, linear ramp up then down
            if (t <= 0.5f) return 2.0f * t;
            return 2.0f * (1.0f - t);
        }

        case TextSelectorShape::Round: {
            // Bell shape: peak at centre, smoothstep on both sides
            if (t <= 0.5f) {
                const f32 u = 2.0f * t;
                return 3.0f * u * u - 2.0f * u * u * u;
            }
            const f32 u = 2.0f * (1.0f - t);
            return 3.0f * u * u - 2.0f * u * u * u;
        }

        case TextSelectorShape::Smooth: {
            // Cubic ease: smoother version, bell-curve
            // Uses smoothstep on both sides independently
            if (t <= 0.5f) {
                const f32 u = 2.0f * t;
                // Eased ramp-up: 1 - (1-u)³
                const f32 one_minus_u = 1.0f - u;
                return 1.0f - one_minus_u * one_minus_u * one_minus_u;
            }
            const f32 u = 2.0f * (1.0f - t);
            const f32 one_minus_u = 1.0f - u;
            return 1.0f - one_minus_u * one_minus_u * one_minus_u;
        }
    }

    return 0.0f;
}

// ═══════════════════════════════════════════════════════════════════════════
// Order application
// ═══════════════════════════════════════════════════════════════════════════

u32 apply_selector_order(
    TextSelectorOrder order,
    u32 original_index,
    u32 total_units,
    u64 random_seed
) {
    if (total_units <= 1) return 0;

    switch (order) {
        case TextSelectorOrder::Forward:
            return original_index;

        case TextSelectorOrder::Reverse:
            return total_units - 1 - original_index;

        case TextSelectorOrder::FromCenter: {
            // Alternate outward from the centre, After Effects-style.
            //
            // The returned value is `perm[original_index]`, i.e. the OUTPUT
            // POSITION where this source index fires during animation.  The
            // examples below list the activation order (which index fires at
            // each time slot `pos 0, 1, 2, ...`).
            //
            // Odd count (e.g. n=5, mid=2, indices [0,1,2,3,4]):
            //   perm table (= position table, indexed by source index):
            //     perm[0] = 3   (edge-left   fires at pos 3)
            //     perm[1] = 1   (centre-left  fires at pos 1)
            //     perm[2] = 0   (centre       fires at pos 0)
            //     perm[3] = 2   (centre-right fires at pos 2)
            //     perm[4] = 4   (edge-right  fires at pos 4)
            //   Activation order (which index fires per pos):
            //     pos 0: index 2 (centre), pos 1: 1 (centre-left),
            //     pos 2: 3 (centre-right), pos 3: 0 (edge-left),  pos 4: 4
            //
            // Even count (e.g. n=4, mid=2, indices [0,1,2,3]):
            //   perm table (= position table, indexed by source index):
            //     perm[0] = 2   (edge-left)
            //     perm[1] = 0   (centre-left   of central pair)
            //     perm[2] = 1   (centre-right  of central pair)
            //     perm[3] = 3   (edge-right)
            //   Activation order:
            //     pos 0: index 1, pos 1: 2,  pos 2: 0, pos 3: 3
            //
            // For larger even counts (e.g. n=6 indices [0,1,2,3,4,5]):
            //   perm[0] = 4, perm[1] = 2, perm[2] = 0, perm[3] = 1,
            //   perm[4] = 3, perm[5] = 5
            //   Activation order: pos 0: idx 2, pos 1: idx 3,
            //                     pos 2: idx 1, pos 3: idx 4,
            //                     pos 4: idx 0, pos 5: idx 5

            const u32 mid = total_units / 2;

            if (total_units % 2 == 1) {
                // Odd count: single centre element at index `mid`.
                if (original_index == mid) return 0;

                // Distance from the centre, on whichever side the index sits.
                if (original_index < mid) {
                    const u32 dist = mid - original_index;        // 1, 2, ...
                    return 2u * dist - 1u;                        // odd output positions
                }
                const u32 dist = original_index - mid;            // 1, 2, ...
                return 2u * dist;                                 // even output positions
            }

            // Even count: central pair = (mid-1, mid), no single centre.
            if (original_index == mid - 1) return 0;             // centre-left
            if (original_index == mid)      return 1;             // centre-right

            if (original_index < mid - 1) {
                // Left of central pair.  pair_dist = 1, 2, 3,... means
                // we're 1, 2, 3,... pairs away from the central pair on the
                // left.  Output positions for pairs: 2, 4, 6, ...
                const u32 pair_dist = (mid - 1) - original_index;
                return 2u + 2u * (pair_dist - 1u);
            }

            // Right of central pair.
            const u32 pair_dist = original_index - mid;
            return 3u + 2u * (pair_dist - 1u);
        }

        case TextSelectorOrder::ToCenter: {
            // Opposite of FromCenter
            const u32 from_centre = apply_selector_order(
                TextSelectorOrder::FromCenter, original_index, total_units, random_seed
            );
            return total_units - 1 - from_centre;
        }

        case TextSelectorOrder::Random: {
            // True Fisher-Yates permutation guaranteed to be a bijection.
            // perm[i] = output position where original index i lands.
            const auto& perm = get_or_build_permutation(random_seed, total_units);
            return perm[original_index];
        }
    }

    return original_index;
}

// ═══════════════════════════════════════════════════════════════════════════
// Deterministic pseudo-random float from (seed, unit_index)
// ═══════════════════════════════════════════════════════════════════════════

// Local hash_combine — avoids dependency on render_graph_hashing.hpp.
[[nodiscard]] inline u64 hash_combine_local(u64 seed, u64 value) {
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
    return seed;
}

f32 hash_to_unit_float(u64 seed, u64 unit_index) {
    // Combine seed and unit_index, then mix to produce a high-quality u64.
    // Two-round mixing via splitmix64 is fast and well-distributed.
    u64 x = hash_combine_local(seed, unit_index);
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    x = x ^ (x >> 31);
    constexpr f64 kInvU64Max = 1.0 / static_cast<f64>(UINT64_MAX);
    return static_cast<f32>(static_cast<f64>(x) * kInvU64Max);
}

// ─── Permutation cache (Fisher-Yates) ────────────────────────────────────
//
// We seed the Fisher-Yates draw from hash_to_unit_float() so that any
// (seed, total_units) pair deterministically produces the same permutation.
// Keeping a thread-local cache avoids recomputing for the (rare) repeats of
// identical (seed ∈ specs, total_units ∈ text length) combinations across
// per-glyph evaluations in a layer.

struct PermutationKey {
    u64 seed;
    u32 total_units;
    bool operator==(const PermutationKey& o) const noexcept {
        return seed == o.seed && total_units == o.total_units;
    }
};

struct PermutationKeyHash {
    size_t operator()(const PermutationKey& k) const noexcept {
        // combine via splitmix64-style mixing so the (seed, total) tuple
        // is well distributed in the hashtable
        size_t h = static_cast<size_t>(k.seed);
        h ^= static_cast<size_t>(k.total_units) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        return h;
    }
};

const std::vector<u32>& get_or_build_permutation(u64 seed, u32 total_units) {
    thread_local std::unordered_map<PermutationKey, std::vector<u32>, PermutationKeyHash> cache;
    PermutationKey key{seed, total_units};
    auto it = cache.find(key);
    if (it != cache.end()) {
        return it->second;
    }

    // Fisher-Yates: start with identity, swap from the right drawing the
    // swap-index from the per-step hash.  This guarantees a true
    // permutation (bijection from [0..total) → [0..total)).
    std::vector<u32> perm(total_units);
    std::iota(perm.begin(), perm.end(), 0u);
    for (u32 i = total_units; i > 1; --i) {
        const u32 u = i - 1;
        const f32 r = hash_to_unit_float(seed, static_cast<u64>(u));
        const u32 raw_j = static_cast<u32>(static_cast<f32>(i) * r);
        const u32 j = (raw_j < i) ? raw_j : u;  // safety clamp (hash_to_unit_float returns [0,1))
        std::swap(perm[u], perm[j]);
    }
    auto inserted = cache.emplace(key, std::move(perm));
    return inserted.first->second;
}

// ─── Whitespace-aware unit exclusion (calls into header-defined is_whitespace_run) ─

bool should_exclude_unit(
    const GlyphSelectorSpec& spec,
    const TextUnitMap& unit_map,
    u32 glyph_index,
    std::string_view source,
    const PlacedGlyphRun* placed
) {
    if (!spec.exclude_spaces) return false;
    if (placed == nullptr) return false;  // backward-compat: opt-in via PlacedGlyphRun
    if (placed->glyphs.empty()) return false;

    switch (spec.unit) {
        case TextSelectorUnit::Glyph:
        case TextSelectorUnit::Grapheme:
        case TextSelectorUnit::Character: {
            // Per-cluster: inspect the byte range of this glyph's cluster.
            if (glyph_index >= placed->glyphs.size()) return false;
            const auto& g = placed->glyphs[glyph_index];
            return is_whitespace_run(source, g.byte_offset, g.byte_len);
        }
        case TextSelectorUnit::Word: {
            // O(1): use precomputed first_glyph_for_word lookup.
            if (glyph_index >= unit_map.glyph_to_word.size()) return false;
            const u32 word_idx = unit_map.glyph_to_word[glyph_index];
            if (word_idx >= unit_map.first_glyph_for_word.size()) return false;
            const u32 gi = unit_map.first_glyph_for_word[word_idx];
            if (gi == UINT32_MAX || gi >= placed->glyphs.size()) return false;
            const auto& g = placed->glyphs[gi];
            return is_whitespace_run(source, g.byte_offset, g.byte_len);
        }
        case TextSelectorUnit::Line: {
            // O(1): use precomputed first_glyph_for_line lookup.
            if (glyph_index >= unit_map.glyph_to_line.size()) return false;
            const u32 line_idx = unit_map.glyph_to_line[glyph_index];
            if (line_idx >= unit_map.first_glyph_for_line.size()) return false;
            const u32 gi = unit_map.first_glyph_for_line[line_idx];
            if (gi == UINT32_MAX || gi >= placed->glyphs.size()) return false;
            const auto& g = placed->glyphs[gi];
            return is_whitespace_run(source, g.byte_offset, g.byte_len);
        }
    }
    return false;
}

} // namespace detail

// ═══════════════════════════════════════════════════════════════════════════
// CompiledSelector — compile-time precomputation
// ═══════════════════════════════════════════════════════════════════════════

CompiledSelector compile_selector(
    const GlyphSelectorSpec& spec,
    const TextUnitMap& unit_map,
    std::string_view source,
    const PlacedGlyphRun* placed
) {
    CompiledSelector c;
    c.spec = &spec;
    c.total_units = unit_map.unit_count(spec.unit);

    // ── Precompute permutation for Random order ───────────────────────
    if (spec.randomize_order && c.total_units > 0) {
        const auto& perm = detail::get_or_build_permutation(spec.random_seed, c.total_units);
        c.random_permutation = perm;
    }

    // ── Precompute whitespace exclusion per unit ──────────────────────
    if (spec.exclude_spaces && placed != nullptr && c.total_units > 0) {
        c.unit_is_whitespace.resize(c.total_units, false);

        if (spec.unit == TextSelectorUnit::Glyph ||
            spec.unit == TextSelectorUnit::Grapheme ||
            spec.unit == TextSelectorUnit::Character) {
            for (u32 gi = 0; gi < c.total_units && gi < placed->glyphs.size(); ++gi) {
                const auto& g = placed->glyphs[gi];
                c.unit_is_whitespace[gi] = detail::is_whitespace_run(
                    source, g.byte_offset, g.byte_len);
            }
        } else if (spec.unit == TextSelectorUnit::Word) {
            for (u32 wi = 0; wi < c.total_units; ++wi) {
                if (wi < unit_map.first_glyph_for_word.size()) {
                    const u32 gi = unit_map.first_glyph_for_word[wi];
                    if (gi != UINT32_MAX && gi < placed->glyphs.size()) {
                        const auto& g = placed->glyphs[gi];
                        c.unit_is_whitespace[wi] = detail::is_whitespace_run(
                            source, g.byte_offset, g.byte_len);
                    }
                }
            }
        } else if (spec.unit == TextSelectorUnit::Line) {
            for (u32 li = 0; li < c.total_units; ++li) {
                if (li < unit_map.first_glyph_for_line.size()) {
                    const u32 gi = unit_map.first_glyph_for_line[li];
                    if (gi != UINT32_MAX && gi < placed->glyphs.size()) {
                        const auto& g = placed->glyphs[gi];
                        c.unit_is_whitespace[li] = detail::is_whitespace_run(
                            source, g.byte_offset, g.byte_len);
                    }
                }
            }
        }
    }

    return c;
}

std::vector<CompiledSelector> compile_selectors(
    const std::vector<GlyphSelectorSpec>& specs,
    const TextUnitMap& unit_map,
    std::string_view source,
    const PlacedGlyphRun* placed
) {
    std::vector<CompiledSelector> compiled;
    compiled.reserve(specs.size());
    for (const auto& spec : specs) {
        compiled.push_back(compile_selector(spec, unit_map, source, placed));
    }
    return compiled;
}

SelectorWeight evaluate_compiled_selector(
    const CompiledSelector& compiled,
    const TextUnitMap& unit_map,
    u32 glyph_index,
    SampleTime time
) {
    const auto& spec = *compiled.spec;
    const u32 total = compiled.total_units;

    if (total == 0) return 0.0f;

    // ── Determine unit index (compute once) ───────────────────────────
    const u32 raw_index = unit_map.unit_index(glyph_index, spec.unit);

    // ── Whitespace exclusion (precomputed, O(1)) ──────────────────────
    if (spec.exclude_spaces && !compiled.unit_is_whitespace.empty()) {
        if (raw_index < compiled.unit_is_whitespace.size()
            && compiled.unit_is_whitespace[raw_index]) {
            return 0.0f;
        }
    }

    if (raw_index >= total) return 0.0f;

    // ── Apply order (uses precomputed permutation for Random) ──────────
    u32 ordered_index;
    if (!compiled.random_permutation.empty()) {
        ordered_index = compiled.random_permutation[raw_index];
    } else {
        ordered_index = detail::apply_selector_order(
            spec.order, raw_index, total, 0);
    }

    // ── Normalise unit position to [0, 1] ─────────────────────────────
    const f32 unit_position = (static_cast<f32>(ordered_index) + 0.5f)
                            / static_cast<f32>(total);

    // ── Evaluate animated parameters ──────────────────────────────────
    const f32 animated_start  = spec.start.evaluate(time)  / 100.0f;
    const f32 animated_end    = spec.end.evaluate(time)    / 100.0f;
    const f32 animated_offset = spec.offset.evaluate(time) / 100.0f;
    const f32 animated_amount = spec.amount.evaluate(time) / 100.0f;

    f32 shifted_position = unit_position - animated_offset;
    shifted_position = shifted_position - std::floor(shifted_position);

    f32 weight = detail::evaluate_selector_shape(
        spec.shape, shifted_position, animated_start, animated_end);

    // ── Apply ease_low / ease_high ────────────────────────────────────
    if (weight > 0.0f && weight < 1.0f) {
        const f32 ease_low_norm  = std::clamp(spec.ease_low  / 100.0f, 0.0f, 1.0f);
        const f32 ease_high_norm = std::clamp(spec.ease_high / 100.0f, 0.0f, 1.0f);

        if (ease_low_norm > 0.0f || ease_high_norm < 1.0f) {
            const f32 abs_range = std::abs(animated_end - animated_start);
            if (abs_range > 1e-7f) {
                const f32 t_raw = (shifted_position - animated_start) / abs_range;
                const f32 t_clamped = std::clamp(t_raw, 0.0f, 1.0f);

                f32 ease_factor = 1.0f;
                if (t_clamped < ease_low_norm) {
                    const f32 et = (ease_low_norm > 1e-7f)
                        ? std::clamp(t_clamped / ease_low_norm, 0.0f, 1.0f)
                        : 1.0f;
                    ease_factor = et * et * (3.0f - 2.0f * et);
                } else if (t_clamped > ease_high_norm) {
                    const f32 one_minus_ease_high = 1.0f - ease_high_norm;
                    const f32 et = (one_minus_ease_high > 1e-7f)
                        ? std::clamp((t_clamped - ease_high_norm) / one_minus_ease_high, 0.0f, 1.0f)
                        : 1.0f;
                    ease_factor = 1.0f - et * et * (3.0f - 2.0f * et);
                }
                weight *= ease_factor;
            }
        }
    }

    weight *= std::clamp(animated_amount, 0.0f, 1.0f);
    return std::clamp(weight, 0.0f, 1.0f);
}

SelectorWeight evaluate_compiled_selectors(
    const std::vector<CompiledSelector>& compiled,
    const TextUnitMap& unit_map,
    u32 glyph_index,
    SampleTime time
) {
    SelectorWeight combined = 0.0f;
    bool first = true;

    for (const auto& cs : compiled) {
        const SelectorWeight w = evaluate_compiled_selector(cs, unit_map, glyph_index, time);

        if (first) {
            combined = w;
            first = false;
            continue;
        }

        switch (cs.spec->combine) {
            case SelectorCombineMode::Replace:
                combined = w;
                break;
            case SelectorCombineMode::Add:
                combined = std::min(combined + w, 1.0f);
                break;
            case SelectorCombineMode::Subtract:
                combined = std::max(combined - w, 0.0f);
                break;
            case SelectorCombineMode::Intersect:
            case SelectorCombineMode::Min:
                combined = std::min(combined, w);
                break;
            case SelectorCombineMode::Max:
                combined = std::max(combined, w);
                break;
        }
    }

    return std::clamp(combined, 0.0f, 1.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Public API (legacy — delegates to compiled path)
// ═══════════════════════════════════════════════════════════════════════════

SelectorWeight evaluate_selector(
    const GlyphSelectorSpec& spec,
    const TextUnitMap& unit_map,
    u32 glyph_index,
    std::string_view source,
    SampleTime time,
    const PlacedGlyphRun* placed
) {
    // Compile on the fly — delegates to the canonical compiled path.
    // Phase 2 will hoist compile_selector() out of the per-frame loop.
    auto compiled = compile_selector(spec, unit_map, source, placed);
    return evaluate_compiled_selector(compiled, unit_map, glyph_index, time);
}

SelectorWeight evaluate_selectors(
    const std::vector<GlyphSelectorSpec>& specs,
    const TextUnitMap& unit_map,
    u32 glyph_index,
    std::string_view source,
    SampleTime time,
    const PlacedGlyphRun* placed
) {
    auto compiled = compile_selectors(specs, unit_map, source, placed);
    return evaluate_compiled_selectors(compiled, unit_map, glyph_index, time);
}

} // namespace chronon3d
