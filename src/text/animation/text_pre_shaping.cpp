// ═══════════════════════════════════════════════════════════════════════════
// text_pre_shaping.cpp — PreShaping property evaluation (FASE 2a)
//
// Evaluates PreShaping properties (CharacterOffset) against the source
// text BEFORE HarfBuzz shaping, so glyph selection, kerning, ligatures,
// and bidi reordering all see the offset characters.
//
// Previously CharacterOffset was applied to GlyphInstanceState.character_offset
// after layout (architecturally wrong — different code points produce
// different glyph shapes, metrics, clusters).
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/animation/text_pre_shaping.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace chronon3d {

namespace {

bool is_preserved_whitespace(char32_t codepoint) {
    return codepoint == U' ' || codepoint == U'\t' || codepoint == U'\r' ||
           codepoint == U'\n' || codepoint == U'\f' || codepoint == U'\v';
}

bool is_unicode_scalar(char32_t codepoint) {
    return codepoint > 0 && codepoint <= 0x10FFFF &&
           !(codepoint >= 0xD800 && codepoint <= 0xDFFF);
}

void append_utf8(std::string& out, char32_t codepoint) {
    if (codepoint <= 0x7F) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════
// apply_character_offset_to_source — Caesar-cipher on UTF-8 code points
// ═══════════════════════════════════════════════════════════════════════════

std::string apply_character_offset_to_source(
    std::string_view source,
    i32 offset
) {
    if (offset == 0 || source.empty()) {
        return std::string{source};
    }

    std::string result;
    result.reserve(source.size());

    const auto* data = source.data();
    const auto* end  = data + source.size();

    while (data < end) {
        // Decode UTF-8 lead byte.
        unsigned char lead = static_cast<unsigned char>(*data);

        if (lead < 0x80) {
            // Single-byte ASCII.
            char c = static_cast<char>(lead);

            // Apply Caesar shift to ASCII letters only.
            if (c >= 'A' && c <= 'Z') {
                i32 shifted = static_cast<i32>(c - 'A') + offset;
                // Euclidean modulo (handles negative offsets correctly).
                shifted = ((shifted % 26) + 26) % 26;
                c = static_cast<char>('A' + shifted);
            } else if (c >= 'a' && c <= 'z') {
                i32 shifted = static_cast<i32>(c - 'a') + offset;
                shifted = ((shifted % 26) + 26) % 26;
                c = static_cast<char>('a' + shifted);
            }
            // Non-letter ASCII: pass through unchanged.
            result.push_back(c);
            ++data;
        } else {
            // Multi-byte UTF-8 sequence: pass through unchanged.
            // Determine length from lead byte.
            size_t seq_len = 1;
            if ((lead & 0xE0) == 0xC0)       seq_len = 2;
            else if ((lead & 0xF0) == 0xE0)  seq_len = 3;
            else if ((lead & 0xF8) == 0xF0)  seq_len = 4;

            for (size_t i = 0; i < seq_len && data < end; ++i) {
                result.push_back(*data++);
            }
        }
    }

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// apply_character_value_to_source — replace non-whitespace code points
// ═══════════════════════════════════════════════════════════════════════════

std::string apply_character_value_to_source(
    std::string_view source,
    char32_t value
) {
    if (!is_unicode_scalar(value) || source.empty()) {
        return std::string{source};
    }

    std::string result;
    result.reserve(source.size());

    size_t pos = 0;
    while (pos < source.size()) {
        const unsigned char lead = static_cast<unsigned char>(source[pos]);
        if (lead < 0x80) {
            const char32_t codepoint = lead;
            if (is_preserved_whitespace(codepoint)) {
                result.push_back(static_cast<char>(lead));
            } else {
                append_utf8(result, value);
            }
            ++pos;
            continue;
        }

        size_t sequence_length = 1;
        if ((lead & 0xE0) == 0xC0)       sequence_length = 2;
        else if ((lead & 0xF0) == 0xE0)  sequence_length = 3;
        else if ((lead & 0xF8) == 0xF0)  sequence_length = 4;

        if (pos + sequence_length > source.size()) {
            result.append(source.substr(pos));
            break;
        }
        append_utf8(result, value);
        pos += sequence_length;
    }

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// has_pre_shaping_properties
// ═══════════════════════════════════════════════════════════════════════════

bool has_pre_shaping_properties(
    const std::vector<TextAnimatorSpec>& animators
) {
    for (const auto& anim : animators) {
        if (!anim.enabled) continue;
        for (const auto& prop : anim.properties) {
            if (std::holds_alternative<CharacterOffsetProperty>(prop) ||
                std::holds_alternative<CharacterValueProperty>(prop)) {
                return true;
            }
        }
    }
    return false;
}

// ═══════════════════════════════════════════════════════════════════════════
// ADR-037 — canonical pre-shaping unit map
// ═══════════════════════════════════════════════════════════════════════════

namespace {

struct SourceSlice {
    std::size_t byte_offset{0};
    std::size_t byte_len{0};
    char32_t value{0};
};

struct Span {
    std::size_t begin{0};
    std::size_t end{0};
    f32 y{0.0f};
};

[[nodiscard]] std::vector<SourceSlice> decode_source_slices(std::string_view source) {
    std::vector<SourceSlice> slices;
    slices.reserve(source.size());
    for (std::size_t pos = 0; pos < source.size();) {
        std::size_t len = 0;
        const char32_t cp = detail::decode_utf8_codepoint_from(source, pos, len);
        if (len == 0 || pos + len > source.size()) len = 1;
        slices.push_back({pos, len, cp});
        pos += len;
    }
    return slices;
}

[[nodiscard]] bool is_combining_mark(char32_t cp) noexcept {
    return (cp >= 0x0300 && cp <= 0x036F) ||
           (cp >= 0x1AB0 && cp <= 0x1AFF) ||
           (cp >= 0x1DC0 && cp <= 0x1DFF) ||
           (cp >= 0x20D0 && cp <= 0x20FF) ||
           (cp >= 0xFE20 && cp <= 0xFE2F);
}

[[nodiscard]] int find_span(const std::vector<Span>& spans, std::size_t offset) noexcept {
    std::size_t lo = 0;
    std::size_t hi = spans.size();
    while (lo < hi) {
        const std::size_t mid = lo + (hi - lo) / 2;
        if (spans[mid].begin <= offset) lo = mid + 1;
        else hi = mid;
    }
    if (lo == 0) return -1;
    --lo;
    if (offset < spans[lo].end) return static_cast<int>(lo);
    return -1;
}

struct SourceChar {
    char32_t value{0};
    std::string utf8;
};

[[nodiscard]] std::vector<SourceChar> decode_source_chars(std::string_view source) {
    std::vector<SourceChar> chars;
    chars.reserve(source.size());
    for (std::size_t pos = 0; pos < source.size();) {
        std::size_t len = 0;
        const char32_t cp = detail::decode_utf8_codepoint_from(source, pos, len);
        if (len == 0 || pos + len > source.size()) len = 1;
        chars.push_back({cp, std::string(source.substr(pos, len))});
        pos += len;
    }
    return chars;
}

void set_source_char(SourceChar& target, char32_t cp) {
    std::string utf8;
    append_utf8(utf8, cp);
    target.value = cp;
    target.utf8 = std::move(utf8);
}

[[nodiscard]] std::string join_source_chars(const std::vector<SourceChar>& chars) {
    std::string out;
    std::size_t total = 0;
    for (const auto& ch : chars) total += ch.utf8.size();
    out.reserve(total);
    for (const auto& ch : chars) out += ch.utf8;
    return out;
}

void apply_offset_to_chars(
    std::vector<SourceChar>& chars,
    const std::vector<f32>& weights,
    i32 offset
) {
    if (offset == 0) return;
    for (std::size_t i = 0; i < chars.size() && i < weights.size(); ++i) {
        const f32 weight = std::clamp(weights[i], 0.0f, 1.0f);
        if (weight <= 0.0f) continue;
        const auto shift = static_cast<i32>(
            std::lround(static_cast<f64>(offset) * static_cast<f64>(weight)));
        if (shift == 0) continue;
        char32_t cp = chars[i].value;
        if (cp >= U'A' && cp <= U'Z') {
            i32 shifted = static_cast<i32>(cp - U'A') + shift;
            shifted = ((shifted % 26) + 26) % 26;
            cp = static_cast<char32_t>(U'A' + shifted);
        } else if (cp >= U'a' && cp <= U'z') {
            i32 shifted = static_cast<i32>(cp - U'a') + shift;
            shifted = ((shifted % 26) + 26) % 26;
            cp = static_cast<char32_t>(U'a' + shifted);
        } else {
            continue;
        }
        set_source_char(chars[i], cp);
    }
}

void apply_value_to_chars(
    std::vector<SourceChar>& chars,
    const std::vector<f32>& weights,
    const PreShapingUnitMap* map,
    char32_t value,
    bool replace_whitespace
) {
    u32 previous_grapheme = std::numeric_limits<u32>::max();
    bool replaced_grapheme = false;
    for (std::size_t i = 0; i < chars.size() && i < weights.size(); ++i) {
        const u32 grapheme = map != nullptr && i < map->codepoints.size()
            ? map->codepoints[i].grapheme_index
            : static_cast<u32>(i);
        if (i == 0 || grapheme != previous_grapheme) {
            previous_grapheme = grapheme;
            replaced_grapheme = false;
            if (weights[i] < 0.5f) continue;
            if (!replace_whitespace && is_preserved_whitespace(chars[i].value)) {
                continue;
            }
            // One replacement per grapheme.  Later codepoints of the same
            // cluster (combining marks, ligature components) are removed so
            // the output never leaves a dangling mark behind.  Keeping the
            // vector slot preserves the canonical map's codepoint indexing for
            // subsequent animators in the stack.
            set_source_char(chars[i], value);
            replaced_grapheme = true;
        } else if (replaced_grapheme) {
            chars[i].value = 0;
            chars[i].utf8.clear();
        }
    }
}

} // namespace

std::shared_ptr<const PreShapingUnitMap> build_pre_shaping_unit_map(
    const PlacedGlyphRun& canonical,
    std::string_view source,
    std::string font_key
) {
    auto map = std::make_shared<PreShapingUnitMap>();
    map->source = std::string{source};
    map->font_key = std::move(font_key);

    const auto slices = decode_source_slices(source);
    map->proxy.ascent = canonical.ascent;
    map->proxy.descent = canonical.descent;
    map->proxy.total_height = canonical.total_height;
    map->proxy.baseline = canonical.baseline;
    map->proxy.font_size = canonical.font_size;
    map->proxy.total_width = static_cast<f32>(slices.size());
    map->proxy.glyphs.resize(slices.size());

    // Canonical clusters, collapsed to one span per shaped cluster.  Merge
    // overlaps before the lookup: `find_span` assumes a sorted, disjoint list.
    std::vector<Span> canonical_spans;
    canonical_spans.reserve(canonical.glyphs.size());
    for (const auto& glyph : canonical.glyphs) {
        if (glyph.byte_len == 0) continue;
        const std::size_t begin = glyph.byte_offset;
        const std::size_t end = std::min(source.size(), begin + glyph.byte_len);
        if (begin >= end) continue;
        canonical_spans.push_back({begin, end, glyph.y});
    }
    std::sort(canonical_spans.begin(), canonical_spans.end(),
              [](const Span& a, const Span& b) {
                  if (a.begin != b.begin) return a.begin < b.begin;
                  return a.end < b.end;
              });
    {
        std::vector<Span> merged;
        merged.reserve(canonical_spans.size());
        for (const auto& span : canonical_spans) {
            if (!merged.empty() && span.begin < merged.back().end) {
                merged.back().end = std::max(merged.back().end, span.end);
            } else {
                merged.push_back(span);
            }
        }
        canonical_spans.swap(merged);
    }

    const std::size_t canonical_count = canonical_spans.size();
    std::vector<Span> extra_spans;
    std::vector<int> span_of_slice(slices.size(), -1);
    const auto span_by_id = [&](int id) -> Span& {
        return id < static_cast<int>(canonical_count)
            ? canonical_spans[static_cast<std::size_t>(id)]
            : extra_spans[static_cast<std::size_t>(id) - canonical_count];
    };

    for (std::size_t i = 0; i < slices.size(); ++i) {
        const auto& slice = slices[i];
        int span = -1;
        if (i > 0 && is_combining_mark(slice.value)) {
            // A combining mark never gets its own unit: extend the preceding
            // cluster so a selector cannot split base and mark.
            span = span_of_slice[i - 1];
            if (span >= 0) {
                Span& previous = span_by_id(span);
                previous.end = std::max(previous.end,
                                        slice.byte_offset + slice.byte_len);
            }
        } else {
            span = find_span(canonical_spans, slice.byte_offset);
        }
        if (span < 0) {
            // Whitespace and other source-only codepoints omitted by shaping
            // get a span of their own (restored by build_text_unit_map's
            // source walk).
            const f32 y = i > 0 && span_of_slice[i - 1] >= 0
                ? span_by_id(span_of_slice[i - 1]).y
                : 0.0f;
            extra_spans.push_back(
                {slice.byte_offset, slice.byte_offset + slice.byte_len, y});
            span = static_cast<int>(canonical_count + extra_spans.size() - 1);
        }
        span_of_slice[i] = span;
    }

    std::vector<Span> spans = canonical_spans;
    spans.insert(spans.end(), extra_spans.begin(), extra_spans.end());
    std::sort(spans.begin(), spans.end(),
              [](const Span& a, const Span& b) {
                  if (a.begin != b.begin) return a.begin < b.begin;
                  return a.end < b.end;
              });
    std::vector<Span> merged;
    merged.reserve(spans.size());
    for (const auto& span : spans) {
        if (!merged.empty() && span.begin < merged.back().end) {
            merged.back().end = std::max(merged.back().end, span.end);
        } else {
            merged.push_back(span);
        }
    }

    std::vector<u32> grapheme_of_slice(slices.size(), 0u);
    for (std::size_t i = 0; i < slices.size(); ++i) {
        const int span = find_span(merged, slices[i].byte_offset);
        grapheme_of_slice[i] = span >= 0 ? static_cast<u32>(span) : 0u;

        auto& glyph = map->proxy.glyphs[i];
        glyph.glyph_id = 0;
        glyph.byte_offset = slices[i].byte_offset;
        glyph.byte_len = slices[i].byte_len;
        glyph.cluster = grapheme_of_slice[i];
        glyph.x = static_cast<f32>(i);
        glyph.y = span >= 0 ? merged[static_cast<std::size_t>(span)].y : 0.0f;
        glyph.is_cluster_start = i == 0 || grapheme_of_slice[i - 1] != grapheme_of_slice[i];
    }

    map->units = build_text_unit_map(map->proxy, source);
    map->codepoints.resize(slices.size());
    map->first_codepoint_for_grapheme.assign(map->units.grapheme_count,
                                             std::numeric_limits<u32>::max());
    for (std::size_t i = 0; i < slices.size(); ++i) {
        auto& cp = map->codepoints[i];
        cp.byte_offset = slices[i].byte_offset;
        cp.byte_len = slices[i].byte_len;
        cp.grapheme_index = map->units.glyph_to_grapheme[i];
        cp.word_index = map->units.glyph_to_word[i];
        cp.line_index = map->units.glyph_to_line[i];
        if (cp.grapheme_index < map->first_codepoint_for_grapheme.size() &&
            map->first_codepoint_for_grapheme[cp.grapheme_index] ==
                std::numeric_limits<u32>::max()) {
            map->first_codepoint_for_grapheme[cp.grapheme_index] =
                static_cast<u32>(i);
        }
    }
    return map;
}

bool has_selector_scoped_pre_shaping(
    const std::vector<TextAnimatorSpec>& animators
) {
    for (const auto& anim : animators) {
        if (!anim.enabled || anim.selectors.empty()) continue;
        for (const auto& prop : anim.properties) {
            if (std::holds_alternative<CharacterOffsetProperty>(prop) ||
                std::holds_alternative<CharacterValueProperty>(prop)) {
                return true;
            }
        }
    }
    return false;
}

// ═══════════════════════════════════════════════════════════════════════════
// evaluate_pre_shaping_source
// ═══════════════════════════════════════════════════════════════════════════

namespace {

std::string evaluate_run_level_source(
    const std::vector<TextAnimatorSpec>& animators,
    std::string_view original_source,
    SampleTime time
) {
    i32 total_offset = 0;
    std::optional<char32_t> replacement;

    for (const auto& anim : animators) {
        if (!anim.enabled) continue;
        for (const auto& prop : anim.properties) {
            if (auto* cop = std::get_if<CharacterOffsetProperty>(&prop)) {
                total_offset += static_cast<i32>(std::lround(cop->offset.evaluate(time)));
            } else if (auto* cvp = std::get_if<CharacterValueProperty>(&prop)) {
                const f32 sampled = cvp->codepoint.evaluate(time);
                if (std::isfinite(sampled)) {
                    const long value = std::lround(sampled);
                    if (value > 0 && value <= 0x10FFFF &&
                        !(value >= 0xD800 && value <= 0xDFFF)) {
                        replacement = static_cast<char32_t>(value);
                    }
                }
            }
        }
    }

    if (total_offset == 0 && !replacement.has_value()) {
        return std::string{original_source};
    }

    std::string transformed = total_offset == 0
        ? std::string{original_source}
        : apply_character_offset_to_source(original_source, total_offset);
    if (replacement.has_value()) {
        transformed = apply_character_value_to_source(transformed, *replacement);
    }
    return transformed;
}

} // namespace

std::string evaluate_pre_shaping_source(
    const std::vector<TextAnimatorSpec>& animators,
    std::string_view original_source,
    SampleTime time
) {
    return evaluate_pre_shaping_source(animators, original_source, time, nullptr);
}

std::string evaluate_pre_shaping_source(
    const std::vector<TextAnimatorSpec>& animators,
    std::string_view original_source,
    SampleTime time,
    const PreShapingUnitMap* unit_map
) {
    if (unit_map == nullptr) {
        return evaluate_run_level_source(animators, original_source, time);
    }

    auto chars = decode_source_chars(original_source);
    if (chars.size() != unit_map->codepoints.size()) {
        // Length-changing transforms are not expected: both offset and value
        // keep one codepoint per codepoint.  Fall back instead of pairing
        // weights with the wrong source characters.
        return evaluate_run_level_source(animators, original_source, time);
    }

    for (const auto& anim : animators) {
        if (!anim.enabled) continue;

        i32 offset = 0;
        std::optional<char32_t> replacement;
        bool has_property = false;
        bool replace_whitespace = false;
        for (const auto& selector : anim.selectors) {
            if (!selector.exclude_spaces) replace_whitespace = true;
        }

        for (const auto& prop : anim.properties) {
            if (auto* cop = std::get_if<CharacterOffsetProperty>(&prop)) {
                offset += static_cast<i32>(std::lround(cop->offset.evaluate(time)));
                has_property = true;
            } else if (auto* cvp = std::get_if<CharacterValueProperty>(&prop)) {
                const f32 sampled = cvp->codepoint.evaluate(time);
                if (std::isfinite(sampled)) {
                    const long value = std::lround(sampled);
                    if (value > 0 && value <= 0x10FFFF &&
                        !(value >= 0xD800 && value <= 0xDFFF)) {
                        replacement = static_cast<char32_t>(value);
                    }
                }
                has_property = true;
            }
        }
        if (!has_property) continue;

        std::vector<f32> weights(chars.size(), 1.0f);
        if (!anim.selectors.empty()) {
            const auto compiled = compile_selectors(
                anim.selectors, unit_map->units, unit_map->source, &unit_map->proxy);
            for (std::size_t i = 0; i < chars.size(); ++i) {
                weights[i] = evaluate_compiled_selectors(
                    compiled, unit_map->units, static_cast<u32>(i), time);
            }
        }

        if (offset != 0) {
            apply_offset_to_chars(chars, weights, offset);
        }
        if (replacement.has_value()) {
            apply_value_to_chars(chars, weights, unit_map, *replacement,
                                 replace_whitespace);
        }
    }
    return join_source_chars(chars);
}

} // namespace chronon3d
