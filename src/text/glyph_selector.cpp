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
        const size_t src_len = source.size();
        std::vector<u32> byte_to_word(src_len + 1, 0);
        std::vector<u32> byte_to_line(src_len + 1, 0);

        for (size_t b = 0; b < src_len;) {
            // Decode UTF-8 to find code point length
            unsigned char lead = static_cast<unsigned char>(source[b]);
            size_t cp_len = 1;
            if ((lead & 0x80u) != 0) {
                if ((lead & 0xE0u) == 0xC0u) cp_len = 2;
                else if ((lead & 0xF0u) == 0xE0u) cp_len = 3;
                else if ((lead & 0xF8u) == 0xF0u) cp_len = 4;
            }
            if (b + cp_len > src_len) cp_len = src_len - b;

            // Check if this byte starts a whitespace or newline
            bool is_space = (cp_len == 1 && (lead == ' ' || lead == '\t'));
            bool is_newline = (cp_len == 1 && lead == '\n');

            if (is_newline) {
                if (in_word) { word_idx++; in_word = false; }
                for (size_t j = 0; j < cp_len && (b + j) < src_len; ++j) {
                    byte_to_word[b + j] = word_idx;
                }
                word_idx++;
                line_idx++;
                for (size_t j = 0; j < cp_len && (b + j) < src_len; ++j) {
                    byte_to_line[b + j] = line_idx;
                }
            } else if (is_space) {
                if (in_word) { word_idx++; in_word = false; }
                for (size_t j = 0; j < cp_len && (b + j) < src_len; ++j) {
                    byte_to_word[b + j] = word_idx;
                    byte_to_line[b + j] = line_idx;
                }
                word_idx++;
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
            byte_to_word[b] = word_idx;
            byte_to_line[b] = line_idx;
        }

        if (in_word) word_idx++;
        map.word_count = word_idx;
        map.line_count = line_idx + 1;

        // Map each glyph to its word/line via its byte offset
        for (size_t g = 0; g < glyph_count; ++g) {
            size_t byte_off = placed.glyphs[g].byte_offset;
            if (byte_off > src_len) byte_off = src_len;
            map.glyph_to_word[g] = byte_to_word[byte_off];
            map.glyph_to_line[g] = byte_to_line[byte_off];
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
            // For word units, look up any glyph belonging to the same word
            // and inspect that cluster's byte range.  We pick the first such
            // glyph so we test the word's first character semantics.
            if (glyph_index >= unit_map.glyph_to_word.size()) return false;
            const u32 word_idx = unit_map.glyph_to_word[glyph_index];
            for (size_t gi = 0; gi < placed->glyphs.size(); ++gi) {
                if (static_cast<u32>(gi) < unit_map.glyph_to_word.size()
                    && unit_map.glyph_to_word[gi] == word_idx) {
                    const auto& g = placed->glyphs[gi];
                    return is_whitespace_run(source, g.byte_offset, g.byte_len);
                }
            }
            return false;
        }
        case TextSelectorUnit::Line: {
            if (glyph_index >= unit_map.glyph_to_line.size()) return false;
            const u32 line_idx = unit_map.glyph_to_line[glyph_index];
            for (size_t gi = 0; gi < placed->glyphs.size(); ++gi) {
                if (static_cast<u32>(gi) < unit_map.glyph_to_line.size()
                    && unit_map.glyph_to_line[gi] == line_idx) {
                    const auto& g = placed->glyphs[gi];
                    return is_whitespace_run(source, g.byte_offset, g.byte_len);
                }
            }
            return false;
        }
    }
    return false;
}

} // namespace detail

// ═══════════════════════════════════════════════════════════════════════════
// Public API
// ═══════════════════════════════════════════════════════════════════════════

SelectorWeight evaluate_selector(
    const GlyphSelectorSpec& spec,
    const TextUnitMap& unit_map,
    u32 glyph_index,
    std::string_view source,
    SampleTime time,
    const PlacedGlyphRun* placed
) {
    // ── Space exclusion (real implementation) ────────────────────────────
    // Requires the placed run for cluster byte-offset access.  When the
    // caller hasn't threaded `placed` through, exclude_spaces is a no-op
    // (preserving backward compatibility).
    if (detail::should_exclude_unit(spec, unit_map, glyph_index, source, placed)) {
        return 0.0f;
    }

    // ── Determine unit index ───────────────────────────────────────────
    const u32 raw_index = unit_map.unit_index(glyph_index, spec.unit);
    const u32 total = unit_map.unit_count(spec.unit);

    if (total == 0) return 0.0f;

    // ── Apply order ────────────────────────────────────────────────────
    u32 ordered_index;
    if (spec.randomize_order) {
        ordered_index = detail::apply_selector_order(
            TextSelectorOrder::Random, raw_index, total, spec.random_seed
        );
    } else {
        ordered_index = detail::apply_selector_order(
            spec.order, raw_index, total, 0
        );
    }

    // ── Normalise unit position to [0, 1] ─────────────────────────────
    // Use centre of unit: (index + 0.5) / total
    const f32 unit_position = (static_cast<f32>(ordered_index) + 0.5f)
                            / static_cast<f32>(total);

    // ── Evaluate animated parameters ──────────────────────────────────
    const f32 animated_start  = spec.start.evaluate(time)  / 100.0f;
    const f32 animated_end    = spec.end.evaluate(time)    / 100.0f;
    const f32 animated_offset = spec.offset.evaluate(time) / 100.0f;
    const f32 animated_amount = spec.amount.evaluate(time) / 100.0f;

    // ── Apply offset with wrap-around ─────────────────────────────────
    f32 shifted_position = unit_position - animated_offset;
    // Wrap to [0, 1] — x - floor(x) always yields [0, 1) in C++
    shifted_position = shifted_position - std::floor(shifted_position);

    // ── Evaluate shape ────────────────────────────────────────────────
    f32 weight = detail::evaluate_selector_shape(
        spec.shape,
        shifted_position,
        animated_start,
        animated_end
    );

    // ── Apply ease_low / ease_high ────────────────────────────────────
    // ease_low: smooth transition at the start of the active range
    // ease_high: smooth transition at the end of the active range
    if (weight > 0.0f && weight < 1.0f) {
        const f32 ease_low_norm  = std::clamp(spec.ease_low  / 100.0f, 0.0f, 1.0f);
        const f32 ease_high_norm = std::clamp(spec.ease_high / 100.0f, 0.0f, 1.0f);

        if (ease_low_norm > 0.0f || ease_high_norm < 1.0f) {
            // Use absolute range to handle reversed start/end
            const f32 abs_range = std::abs(animated_end - animated_start);
            if (abs_range > 1e-7f) {
                const f32 t_raw = (shifted_position - animated_start) / abs_range;
                const f32 t_clamped = std::clamp(t_raw, 0.0f, 1.0f);

                f32 ease_factor = 1.0f;
                if (t_clamped < ease_low_norm) {
                    // In ease-low region: smooth from 0 to ease_low_norm
                    const f32 et = (ease_low_norm > 1e-7f)
                        ? std::clamp(t_clamped / ease_low_norm, 0.0f, 1.0f)
                        : 1.0f;
                    ease_factor = et * et * (3.0f - 2.0f * et); // smoothstep
                } else if (t_clamped > ease_high_norm) {
                    // In ease-high region
                    const f32 one_minus_ease_high = 1.0f - ease_high_norm;
                    const f32 et = (one_minus_ease_high > 1e-7f)
                        ? std::clamp((t_clamped - ease_high_norm) / one_minus_ease_high, 0.0f, 1.0f)
                        : 1.0f;
                    ease_factor = 1.0f - et * et * (3.0f - 2.0f * et); // reverse smoothstep
                }
                weight *= ease_factor;
            }
        }
    }

    // ── Apply amount multiplier ───────────────────────────────────────
    weight *= std::clamp(animated_amount, 0.0f, 1.0f);

    return std::clamp(weight, 0.0f, 1.0f);
}

SelectorWeight evaluate_selectors(
    const std::vector<GlyphSelectorSpec>& specs,
    const TextUnitMap& unit_map,
    u32 glyph_index,
    std::string_view source,
    SampleTime time,
    const PlacedGlyphRun* placed
) {
    SelectorWeight combined = 0.0f;
    bool first = true;

    for (const auto& spec : specs) {
        const SelectorWeight w = evaluate_selector(spec, unit_map, glyph_index, source, time, placed);

        if (first) {
            combined = w;
            first = false;
            continue;
        }

        switch (spec.combine) {
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

} // namespace chronon3d
